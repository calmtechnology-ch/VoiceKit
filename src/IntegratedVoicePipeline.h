#pragma once

/**
 * @file IntegratedVoicePipeline.h
 * @brief En-tête public pour le pipeline vocal intégré VoiceKit v0.6.5.
 * 
 * Expose les abstractions complètes d'orchestration :
 * - IntegratedVoicePipeline : orchestrateur de cycle complet (Activation -> STT -> Décision -> Tool/IA -> TTS -> READY)
 * - VoiceCommandDispatcher : routeur de commandes vocales locales déterministe avec normalisation
 * - VoicePipelineConfig : configuration centralisée des politiques et délais
 */

#include "core/pipeline/VoicePipelineConfig.h"
#include "core/pipeline/VoiceCommandDispatcher.h"
#include "core/pipeline/IntegratedVoicePipeline.h"

