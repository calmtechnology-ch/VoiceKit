#include "ToolRegistry.h"
#include <algorithm>

ToolRegistry::ToolRegistry(size_t maxCapacity)
    : _maxCapacity(maxCapacity),
      _confirmationTimeoutMs(DEFAULT_CONFIRMATION_TIMEOUT_MS) {
    _tools.reserve(_maxCapacity);
}

ToolRegistry::~ToolRegistry() {
    clear();
}

void ToolRegistry::setMaxCapacity(size_t maxCapacity) {
    _maxCapacity = maxCapacity;
    if (_maxCapacity > _tools.capacity()) {
        _tools.reserve(_maxCapacity);
    }
}

bool ToolRegistry::registerTool(const ToolDefinition& def) {
    if (_tools.size() >= _maxCapacity) {
        return false;
    }
    if (hasTool(def.id)) {
        return false;
    }
    String reason;
    if (!def.isValid(reason)) {
        return false;
    }
    _tools.push_back(def);
    return true;
}

bool ToolRegistry::registerTool(const String& id,
                                const String& description,
                                std::function<ToolResult()> handler,
                                ToolSecurityPolicy policy) {
    if (!handler) return false;
    ToolDefinition def(id, description, policy);
    def.setHandler([handler](const ToolArgs&) -> ToolResult {
        return handler();
    });
    return registerTool(def);
}

bool ToolRegistry::registerTool(const String& id,
                                const String& description,
                                ToolHandler handler,
                                const std::vector<ToolParam>& params,
                                ToolSecurityPolicy policy,
                                bool allowUnknownParams) {
    if (!handler) return false;
    ToolDefinition def(id, description, policy);
    def.parameters = params;
    def.allowUnknownParams = allowUnknownParams;
    def.setHandler(std::move(handler));
    return registerTool(def);
}

bool ToolRegistry::unregisterTool(const String& id) {
    for (auto it = _tools.begin(); it != _tools.end(); ++it) {
        if (it->id == id) {
            _tools.erase(it);
            return true;
        }
    }
    return false;
}

void ToolRegistry::clear() {
    _tools.clear();
    _pendingConfirmations.clear();
    _defaultExecutor.cancelDeferred();
}

size_t ToolRegistry::getToolCount() const {
    return _tools.size();
}

bool ToolRegistry::hasTool(const String& id) const {
    return findTool(id) != nullptr;
}

const ToolDefinition* ToolRegistry::findTool(const String& id) const {
    for (const auto& t : _tools) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::vector<String> ToolRegistry::getToolIds() const {
    std::vector<String> ids;
    ids.reserve(_tools.size());
    for (const auto& t : _tools) {
        ids.push_back(t.id);
    }
    return ids;
}

ToolResult ToolRegistry::execute(const String& id,
                                 const ToolArgs& args,
                                 const String& confirmationToken) {
    String reqId = generateRequestId();
    emitEvent(ToolEventType::TOOL_REQUESTED, id, reqId, "Execution demandee");

    const ToolDefinition* def = findTool(id);
    if (!def) {
        emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Commande introuvable");
        return ToolResult::notFound("Commande introuvable : " + id);
    }

    if (_authChecker && !_authChecker(*def, args)) {
        emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Refus par la politique d'autorisation");
        return ToolResult::permissionDenied("Action refusee par la politique d'autorisation : " + id);
    }

    ToolResult valRes = ToolValidator::validate(*def, args);
    if (!valRes.isSuccess()) {
        emitEvent(ToolEventType::TOOL_FAILED, id, reqId, valRes.error);
        return valRes;
    }

    if (def->securityPolicy == ToolSecurityPolicy::SENSITIVE) {
        if (confirmationToken.isEmpty()) {
            String token = generateConfirmationToken();
            PendingConfirmation pending;
            pending.token = token;
            pending.requestId = reqId;
            pending.toolId = id;
            pending.args = args;
            pending.createdAtMs = millis();
            pending.timeoutMs = _confirmationTimeoutMs;
            pending.used = false;
            _pendingConfirmations.push_back(std::move(pending));

            emitEvent(ToolEventType::TOOL_CONFIRMATION_REQUIRED, id, reqId, "Confirmation requise [token=" + token + "]");
            return ToolResult::confirmationRequired(token, "Confirmation requise pour la commande sensible : " + id);
        } else {
            PendingConfirmation* target = nullptr;
            for (auto& req : _pendingConfirmations) {
                if (req.token == confirmationToken) {
                    target = &req;
                    break;
                }
            }
            if (!target) {
                emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Jeton introuvable");
                return ToolResult::permissionDenied("Jeton de confirmation introuvable ou invalide");
            }
            if (target->used) {
                emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Jeton deja utilise");
                return ToolResult::permissionDenied("Jeton de confirmation deja utilise (protection anti-rejeu)");
            }
            if (millis() - target->createdAtMs > target->timeoutMs) {
                emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Jeton expire");
                return ToolResult::permissionDenied("Jeton de confirmation expire");
            }
            if (target->toolId != id) {
                emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Jeton associe a un autre outil");
                return ToolResult::permissionDenied("Jeton de confirmation non associe a cette commande");
            }
            target->used = true;
        }
    }

    IToolExecutor& exec = getExecutor();
    if (exec.isBusy()) {
        emitEvent(ToolEventType::TOOL_FAILED, id, reqId, "Executeur occupe");
        return ToolResult::busy("Executeur occupe : une commande est deja en cours");
    }

    emitEvent(ToolEventType::TOOL_STARTED, id, reqId, "Demarrage de l'execution");
    ToolResult res = exec.execute(*def, args);

    if (res.isSuccess()) {
        emitEvent(ToolEventType::TOOL_COMPLETED, id, reqId, res.output);
    } else {
        emitEvent(ToolEventType::TOOL_FAILED, id, reqId, res.error);
    }

    return res;
}

ToolResult ToolRegistry::confirm(const String& confirmationToken) {
    PendingConfirmation* target = nullptr;
    for (auto& req : _pendingConfirmations) {
        if (req.token == confirmationToken) {
            target = &req;
            break;
        }
    }

    if (!target) {
        emitEvent(ToolEventType::TOOL_FAILED, "", "", "Jeton introuvable");
        return ToolResult::permissionDenied("Jeton de confirmation introuvable ou invalide");
    }

    if (target->used) {
        emitEvent(ToolEventType::TOOL_FAILED, target->toolId, target->requestId, "Jeton deja utilise");
        return ToolResult::permissionDenied("Jeton de confirmation deja utilise (protection anti-rejeu)");
    }

    if (millis() - target->createdAtMs > target->timeoutMs) {
        emitEvent(ToolEventType::TOOL_FAILED, target->toolId, target->requestId, "Jeton expire");
        return ToolResult::permissionDenied("Jeton de confirmation expire");
    }

    target->used = true;

    const ToolDefinition* def = findTool(target->toolId);
    if (!def) {
        emitEvent(ToolEventType::TOOL_FAILED, target->toolId, target->requestId, "Commande introuvable");
        return ToolResult::notFound("Commande introuvable lors de la confirmation : " + target->toolId);
    }

    IToolExecutor& exec = getExecutor();
    if (exec.isBusy()) {
        emitEvent(ToolEventType::TOOL_FAILED, target->toolId, target->requestId, "Executeur occupe");
        return ToolResult::busy("Executeur occupe lors de la confirmation");
    }

    emitEvent(ToolEventType::TOOL_STARTED, target->toolId, target->requestId, "Demarrage apres confirmation");
    ToolResult res = exec.execute(*def, target->args);

    if (res.isSuccess()) {
        emitEvent(ToolEventType::TOOL_COMPLETED, target->toolId, target->requestId, res.output);
    } else {
        emitEvent(ToolEventType::TOOL_FAILED, target->toolId, target->requestId, res.error);
    }

    return res;
}

void ToolRegistry::cancelPending() {
    _pendingConfirmations.clear();
    _defaultExecutor.cancelDeferred();
}

bool ToolRegistry::isBusy() const {
    return const_cast<ToolRegistry*>(this)->getExecutor().isBusy();
}

void ToolRegistry::update() {
    purgeExpiredConfirmations();
    getExecutor().update();
}

void ToolRegistry::emitEvent(ToolEventType type, const String& toolId, const String& requestId, const String& msg) {
    ToolEvent evt;
    evt.type = type;
    evt.toolId = toolId;
    evt.requestId = requestId;
    evt.message = msg;
    evt.timestampMs = millis();

    if (_globalEventCallback) {
        _globalEventCallback(evt);
    }

    size_t idx = static_cast<size_t>(type);
    if (idx < 5 && _typedEventCallbacks[idx]) {
        _typedEventCallbacks[idx](evt);
    }
}

String ToolRegistry::generateRequestId() {
    return "REQ-" + String(++_requestCounter);
}

String ToolRegistry::generateConfirmationToken() {
    return "CONF-" + String(++_tokenCounter) + "-" + String(millis());
}

void ToolRegistry::purgeExpiredConfirmations() {
    uint32_t now = millis();
    auto it = _pendingConfirmations.begin();
    while (it != _pendingConfirmations.end()) {
        // Supprime les jetons déjà consommés ou expirés depuis plus du double de leur durée
        if (it->used || (now - it->createdAtMs > it->timeoutMs * 2)) {
            it = _pendingConfirmations.erase(it);
        } else {
            ++it;
        }
    }
}
