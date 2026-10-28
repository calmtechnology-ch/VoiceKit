/**
 * @file esp_sr_wakenet_port.c
 * @brief Pont d'intégration officiel Espressif ESP-SR WakeNet9l pour ESP32-S3
 *        sous Arduino-ESP32 (ESP-IDF 4.4.x / 5.x).
 *
 * Fournit :
 * - Le chargeur de modèles ESP-SR (`esp_srmodel_init`, `srmodel_load`, `esp_srmodel_filter`, `esp_srmodel_deinit`)
 *   avec repli automatique sur le modèle officiel « Bonjour ESP » (`wn9l_fr_bonjouresp_tts3`)
 *   embarqué en Flash (.rodata) lorsqu'aucune partition dédiée `model` n'est présente.
 * - La table de résolution `esp_wn_handle_from_name` pointant vers `esp_sr_wakenet9l_quantized`
 *   et `esp_sr_wakenet9_quantized`.
 * - Les adaptateurs DSP (`esp_dl_dotprod_f32`, `dl_rfft_f32_init/run/deinit`) reliés aux
 *   routines matérielles Xtensa FPU/SIMD de `libc_speech_features.a` (`mfcc_dsp.S.obj`, `fft.c.obj`).
 */

#if defined(ESP32) || defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#if (defined(ESP32) || defined(ESP_PLATFORM)) && defined(CONFIG_IDF_TARGET_ESP32S3)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "spi_flash_mmap.h"
#else
#include "esp_spi_flash.h"
#endif

#include "include/model_path.h"
#include "include/esp_wn_iface.h"
#include "include/esp_wn_models.h"
#include "include/esp_sr_debug.h"

static const char *TAG = "ESP_SR_WAKENET";

// Modèle officiel Espressif "Bonjour ESP" (wn9l_fr_bonjouresp_tts3) embarqué en Flash (.rodata)
extern const uint8_t voicekit_srmodels_bonjouresp_bin[];
extern const size_t voicekit_srmodels_bonjouresp_bin_len;

// Symboles WakeNet9 / WakeNet9l issus de libesp_sr_wakenet_s3.a
extern const esp_wn_iface_t esp_sr_wakenet9_quantized;
extern const esp_wn_iface_t esp_sr_wakenet9l_quantized;

// Symbole DSP issu de libc_speech_features (mfcc_dsp.S.obj)
extern int dsps_dotprod_f32_ae32_mfcc(const float *src1, const float *src2, float *dest, int len);

// =============================================================================
// 1. Debug mode ESP-SR
// =============================================================================
static int s_esp_sr_debug_mode = 0;

void esp_sr_set_debug_mode(int flag) {
    s_esp_sr_debug_mode = flag;
}

int esp_sr_get_debug_mode(void) {
    return s_esp_sr_debug_mode;
}

// =============================================================================
// 2. Résolution du modèle WakeNet par nom (esp_wn_models.h)
// =============================================================================
const esp_wn_iface_t *esp_wn_handle_from_name(const char *model_name) {
    if (model_name == NULL) {
        return NULL;
    }

    char prefix[32];
    size_t i = 0;
    while (model_name[i] != '\0' && model_name[i] != '_' && i < sizeof(prefix) - 1) {
        prefix[i] = model_name[i];
        i++;
    }
    prefix[i] = '\0';

    if (strcmp(prefix, "wn9l") == 0) {
        return &esp_sr_wakenet9l_quantized;
    }
    if (strcmp(prefix, "wn9") == 0) {
        return &esp_sr_wakenet9_quantized;
    }
    return NULL;
}

char *esp_wn_wakeword_from_name(const char *model_name) {
    if (model_name == NULL) {
        return NULL;
    }
    const char *sep = strchr(model_name, '_');
    if (sep == NULL) {
        return NULL;
    }
    return (char *)(sep + 1);
}

// =============================================================================
// 3. Adaptateurs DSP officiels esp-dl (esp_dl_dotprod_f32 & dl_rfft_f32)
// =============================================================================
void esp_dl_dotprod_f32(float *x, float *y, float *out, int len) {
    if (len <= 0) {
        if (out) {
            *out = 0.0f;
        }
        return;
    }
    dsps_dotprod_f32_ae32_mfcc(x, y, out, len);
}

typedef struct {
    float re;
    float im;
} voicekit_dl_fc32_t;

typedef struct {
    int fft_point;
    int log2n;
    float *fft_table;
    float *rfft_table;
    uint16_t *bitrev_table;
    int bitrev_size;
} voicekit_dl_fft_f32_t;

static int voicekit_dl_power_of_two(uint32_t n) {
    int pos = 0;
    if (n >= (1U << 16)) { n >>= 16; pos += 16; }
    if (n >= (1U << 8))  { n >>= 8;  pos += 8;  }
    if (n >= (1U << 4))  { n >>= 4;  pos += 4;  }
    if (n >= (1U << 2))  { n >>= 2;  pos += 2;  }
    if (n >= (1U << 1))  { pos += 1; }
    return pos;
}

static void voicekit_dl_bitrev2r_fc32_ansi(float *data, int N) {
    int j = 0, k;
    float r_temp, i_temp;
    for (int i = 1; i < (N - 1); i++) {
        k = N >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
        if (i < j) {
            r_temp = data[j * 2];
            data[j * 2] = data[i * 2];
            data[i * 2] = r_temp;
            i_temp = data[j * 2 + 1];
            data[j * 2 + 1] = data[i * 2 + 1];
            data[i * 2 + 1] = i_temp;
        }
    }
}

static void voicekit_dl_fft2r_fc32_ansi(float *data, int N, float *w) {
    int ie = 1, ia, m;
    float re_temp, im_temp, c, s;
    for (int N2 = N / 2; N2 > 0; N2 >>= 1) {
        ia = 0;
        for (int j = 0; j < ie; j++) {
            c = w[2 * j];
            s = w[2 * j + 1];
            for (int i = 0; i < N2; i++) {
                m = ia + N2;
                re_temp = c * data[2 * m] + s * data[2 * m + 1];
                im_temp = c * data[2 * m + 1] - s * data[2 * m];
                data[2 * m] = data[2 * ia] - re_temp;
                data[2 * m + 1] = data[2 * ia + 1] - im_temp;
                data[2 * ia] = data[2 * ia] + re_temp;
                data[2 * ia + 1] = data[2 * ia + 1] + im_temp;
                ia++;
            }
            ia += N2;
        }
        ie <<= 1;
    }
}

static void voicekit_dl_fft4r_fc32_ansi(float *data, int length, float *table) {
    voicekit_dl_fc32_t bfly[4];
    int log2N = voicekit_dl_power_of_two((uint32_t)length);
    int log4N = log2N >> 1;
    if ((log2N & 0x01) != 0) {
        return;
    }
    int m = 2;
    int wind_step = 2;
    while (log4N > 0) {
        length = length >> 2;
        for (int j = 0; j < m; j += 2) {
            int start_index = j * (length << 1);
            voicekit_dl_fc32_t *ptrc0 = (voicekit_dl_fc32_t *)data + start_index;
            voicekit_dl_fc32_t *ptrc1 = ptrc0 + length;
            voicekit_dl_fc32_t *ptrc2 = ptrc1 + length;
            voicekit_dl_fc32_t *ptrc3 = ptrc2 + length;
            voicekit_dl_fc32_t *winc0 = (voicekit_dl_fc32_t *)table;
            voicekit_dl_fc32_t *winc1 = winc0;
            voicekit_dl_fc32_t *winc2 = winc0;

            for (int k = 0; k < length; k++) {
                voicekit_dl_fc32_t in0 = *ptrc0;
                voicekit_dl_fc32_t in2 = *ptrc2;
                voicekit_dl_fc32_t in1 = *ptrc1;
                voicekit_dl_fc32_t in3 = *ptrc3;

                bfly[0].re = in0.re + in2.re + in1.re + in3.re;
                bfly[0].im = in0.im + in2.im + in1.im + in3.im;
                bfly[1].re = in0.re - in2.re + in1.im - in3.im;
                bfly[1].im = in0.im - in2.im - in1.re + in3.re;
                bfly[2].re = in0.re + in2.re - in1.re - in3.re;
                bfly[2].im = in0.im + in2.im - in1.im - in3.im;
                bfly[3].re = in0.re - in2.re - in1.im + in3.im;
                bfly[3].im = in0.im - in2.im + in1.re - in3.re;

                *ptrc0 = bfly[0];
                ptrc1->re = bfly[1].re * winc0->re + bfly[1].im * winc0->im;
                ptrc1->im = bfly[1].im * winc0->re - bfly[1].re * winc0->im;
                ptrc2->re = bfly[2].re * winc1->re + bfly[2].im * winc1->im;
                ptrc2->im = bfly[2].im * winc1->re - bfly[2].re * winc1->im;
                ptrc3->re = bfly[3].re * winc2->re + bfly[3].im * winc2->im;
                ptrc3->im = bfly[3].im * winc2->re - bfly[3].re * winc2->im;

                winc0 += 1 * wind_step;
                winc1 += 2 * wind_step;
                winc2 += 3 * wind_step;
                ptrc0++; ptrc1++; ptrc2++; ptrc3++;
            }
        }
        m <<= 2;
        wind_step <<= 2;
        log4N--;
    }
}

static void voicekit_dl_bitrev4r_fc32_ansi(float *data, int N) {
    int log2N = voicekit_dl_power_of_two((uint32_t)N);
    int log4N = log2N >> 1;
    if ((log2N & 0x01) != 0) {
        return;
    }
    float r_temp, i_temp;
    for (int i = 0; i < N; i++) {
        int cnt = log4N;
        int xx = 0;
        int j = i;
        while (cnt > 0) {
            int bits2 = j & 0x3;
            xx = (xx << 2) + bits2;
            j >>= 2;
            cnt--;
        }
        if (i < xx) {
            r_temp = data[i * 2 + 0];
            i_temp = data[i * 2 + 1];
            data[i * 2 + 0] = data[xx * 2 + 0];
            data[i * 2 + 1] = data[xx * 2 + 1];
            data[xx * 2 + 0] = r_temp;
            data[xx * 2 + 1] = i_temp;
        }
    }
}

static void voicekit_dl_rfft_post_proc_fc32_ansi(float *data, int N, const float *table) {
    voicekit_dl_fc32_t *result = (voicekit_dl_fc32_t *)data;
    float tmp_re = result[0].re;
    result[0].re = tmp_re + result[0].im;
    result[0].im = tmp_re - result[0].im;

    voicekit_dl_fc32_t f1k, f2k;
    for (int k = 1; k <= N / 2; k++) {
        voicekit_dl_fc32_t fpk = result[k];
        voicekit_dl_fc32_t fpnk = result[N - k];
        f1k.re = fpk.re + fpnk.re;
        f1k.im = fpk.im - fpnk.im;
        f2k.re = fpk.re - fpnk.re;
        f2k.im = fpk.im + fpnk.im;

        float c = -table[k * 2 - 1];
        float s = -table[k * 2 - 2];
        voicekit_dl_fc32_t tw;
        tw.re = c * f2k.re - s * f2k.im;
        tw.im = s * f2k.re + c * f2k.im;

        result[k].re = 0.5f * (f1k.re + tw.re);
        result[k].im = 0.5f * (f1k.im + tw.im);
        result[N - k].re = 0.5f * (f1k.re - tw.re);
        result[N - k].im = 0.5f * (tw.im - f1k.im);
    }
}

void *dl_rfft_f32_init(int fft_point, int caps) {
    (void)caps;
    if (fft_point < 8 || (fft_point & (fft_point - 1)) != 0) {
        return NULL;
    }
    voicekit_dl_fft_f32_t *handle = (voicekit_dl_fft_f32_t *)calloc(1, sizeof(voicekit_dl_fft_f32_t));
    if (!handle) {
        return NULL;
    }
    handle->fft_point = fft_point;
    handle->log2n = voicekit_dl_power_of_two((uint32_t)fft_point);

    handle->rfft_table = (float *)malloc((size_t)fft_point * sizeof(float));
    if (!handle->rfft_table) {
        free(handle);
        return NULL;
    }
    for (int i = 1; i <= (fft_point >> 1); i++) {
        float angle = (float)(2.0 * M_PI * (double)i / (double)fft_point);
        handle->rfft_table[2 * i - 2] = cosf(angle);
        handle->rfft_table[2 * i - 1] = sinf(angle);
    }

    if (handle->log2n % 2 == 1) {
        handle->fft_table = (float *)malloc((size_t)fft_point * 2U * sizeof(float));
        if (!handle->fft_table) {
            free(handle->rfft_table);
            free(handle);
            return NULL;
        }
        for (int i = 0; i < fft_point; i++) {
            float angle = (float)(2.0 * M_PI * (double)i / (double)fft_point);
            handle->fft_table[2 * i] = cosf(angle);
            handle->fft_table[2 * i + 1] = sinf(angle);
        }
    } else {
        int half = fft_point >> 1;
        handle->fft_table = (float *)malloc((size_t)half * sizeof(float));
        if (!handle->fft_table) {
            free(handle->rfft_table);
            free(handle);
            return NULL;
        }
        float e = (float)(2.0 * M_PI / (double)half);
        for (int i = 0; i < (half >> 1); i++) {
            handle->fft_table[2 * i] = cosf((float)i * e);
            handle->fft_table[2 * i + 1] = sinf((float)i * e);
        }
        voicekit_dl_bitrev2r_fc32_ansi(handle->fft_table, half >> 1);
    }
    return handle;
}

int dl_rfft_f32_run(void *h, float *data) {
    if (!h || !data) {
        return -1;
    }
    voicekit_dl_fft_f32_t *handle = (voicekit_dl_fft_f32_t *)h;
    int fft_point = handle->fft_point;
    if (handle->log2n % 2 == 1) {
        voicekit_dl_fft4r_fc32_ansi(data, fft_point >> 1, handle->fft_table);
        voicekit_dl_bitrev4r_fc32_ansi(data, fft_point >> 1);
    } else {
        voicekit_dl_fft2r_fc32_ansi(data, fft_point >> 1, handle->fft_table);
        voicekit_dl_bitrev2r_fc32_ansi(data, fft_point >> 1);
    }
    voicekit_dl_rfft_post_proc_fc32_ansi(data, fft_point >> 1, handle->rfft_table);
    return 0;
}

void dl_rfft_f32_deinit(void *h) {
    if (!h) {
        return;
    }
    voicekit_dl_fft_f32_t *handle = (voicekit_dl_fft_f32_t *)h;
    if (handle->fft_table) {
        free(handle->fft_table);
    }
    if (handle->rfft_table) {
        free(handle->rfft_table);
    }
    if (handle->bitrev_table) {
        free(handle->bitrev_table);
    }
    free(handle);
}

// =============================================================================
// 4. Chargeur de modèles ESP-SR (model_path.h)
// =============================================================================
static const char *SRMODE_BASE_PATH = "/srmodel";
static srmodel_list_t *static_srmodels = NULL;
static int srmodel_init_refcount = 0;

void set_model_base_path(const char *base_path) {
    SRMODE_BASE_PATH = base_path;
}

char *get_model_base_path(void) {
    return (char *)SRMODE_BASE_PATH;
}

srmodel_list_t *get_static_srmodels(void) {
    return static_srmodels;
}

char *get_model_info(char *data, int size) {
    char *model_info = NULL;
    while (size > 0) {
        if (*data == '#') {
            while (*data != '\n' && size > 1) {
                data++;
                size--;
            }
            data++;
            size--;
            continue;
        } else if (data != NULL && size > 0) {
            model_info = (char *)malloc((size + 1) * sizeof(char));
            if (!model_info) {
                return NULL;
            }
            memcpy(model_info, data, size);
            if (model_info[size - 1] == '\n') {
                model_info[size - 1] = '\0';
            }
            model_info[size] = '\0';
            break;
        }
    }
    return model_info;
}

char *get_wake_words_from_info(char *model_info) {
    if (model_info == NULL) {
        return NULL;
    }
    int info_len = strlen(model_info);
    char *temp = (char *)malloc(info_len + 1);
    if (!temp) {
        return NULL;
    }
    memcpy(temp, model_info, info_len);
    temp[info_len] = '\0';
    char *token = strtok(temp, "_");
    char *wake_words = NULL;
    int word_len = 0;
    int i = 1;
    while (token != NULL) {
        if (i % 4 == 3) {
            if (wake_words == NULL) {
                word_len = strlen(token) + 1;
                wake_words = (char *)malloc(word_len);
                memcpy(wake_words, token, word_len - 1);
                wake_words[word_len - 1] = '\0';
            } else {
                word_len += strlen(token) + 1;
                wake_words = (char *)realloc(wake_words, word_len);
                strcat(wake_words, ";");
                strcat(wake_words, token);
                wake_words[word_len - 1] = '\0';
            }
        }
        token = strtok(NULL, "_");
        i++;
    }
    free(temp);
    return wake_words;
}

static srmodel_list_t *srmodel_list_alloc(void) {
    srmodel_list_t *models = (srmodel_list_t *)malloc(sizeof(srmodel_list_t));
    if (!models) {
        return NULL;
    }
    models->model_data = NULL;
    models->model_name = NULL;
    models->model_info = NULL;
    models->num = 0;
    models->partition = NULL;
    models->mmap_handle = NULL;
    return models;
}

static uint32_t read_int32(char *data) {
    uint8_t *bytes = (uint8_t *)data;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

srmodel_list_t *srmodel_load(const void *root) {
    if (root == NULL) {
        return NULL;
    }
    if (static_srmodels == NULL) {
        static_srmodels = srmodel_list_alloc();
        if (!static_srmodels) {
            return NULL;
        }
    } else if (static_srmodels->num > 0 && static_srmodels->model_data != NULL) {
        set_model_base_path(NULL);
        return static_srmodels;
    }

    srmodel_list_t *models = static_srmodels;
    char *start = (char *)root;
    char *data = (char *)root;
    int str_len = SRMODEL_STRING_LENGTH;
    int int_len = 4;

    models->num = read_int32(data);
    data += int_len;
    models->model_data = (srmodel_data_t **)malloc(sizeof(srmodel_data_t *) * models->num);
    models->model_name = (char **)malloc(sizeof(char *) * models->num);
    models->model_info = (char **)malloc(sizeof(char *) * models->num);

    for (int i = 0; i < models->num; i++) {
        srmodel_data_t *model_data = (srmodel_data_t *)malloc(sizeof(srmodel_data_t));
        models->model_info[i] = NULL;
        models->model_name[i] = (char *)malloc((SRMODEL_STRING_LENGTH + 1) * sizeof(char));
        strncpy(models->model_name[i], data, SRMODEL_STRING_LENGTH);
        models->model_name[i][SRMODEL_STRING_LENGTH] = '\0';
        data += str_len;

        int file_num = read_int32(data);
        model_data->num = file_num;
        data += int_len;
        model_data->files = (char **)malloc(sizeof(char *) * file_num);
        model_data->data = (char **)malloc(sizeof(void *) * file_num);
        model_data->sizes = (int *)malloc(sizeof(int) * file_num);

        for (int j = 0; j < file_num; j++) {
            model_data->files[j] = data;
            data += str_len;
            int index = read_int32(data);
            data += int_len;
            model_data->data[j] = start + index;
            int size = read_int32(data);
            data += int_len;
            model_data->sizes[j] = size;

            if (strcmp(model_data->files[j], "_MODEL_INFO_") == 0) {
                models->model_info[i] = get_model_info(model_data->data[j], model_data->sizes[j]);
            }
        }
        models->model_data[i] = model_data;
    }
    set_model_base_path(NULL);
    return models;
}

srmodel_list_t *srmodel_mmap_init(const esp_partition_t *partition) {
    if (static_srmodels != NULL && static_srmodels->num > 0) {
        return static_srmodels;
    }
    if (static_srmodels == NULL) {
        static_srmodels = srmodel_list_alloc();
    }
    srmodel_list_t *models = static_srmodels;
    const void *root = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    models->mmap_handle = (esp_partition_mmap_handle_t *)malloc(sizeof(esp_partition_mmap_handle_t));
    if (esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &root,
                           (esp_partition_mmap_handle_t *)models->mmap_handle) != ESP_OK) {
        free(models->mmap_handle);
        models->mmap_handle = NULL;
        free(models);
        static_srmodels = NULL;
        return NULL;
    }
#else
    models->mmap_handle = (spi_flash_mmap_handle_t *)malloc(sizeof(spi_flash_mmap_handle_t));
    if (esp_partition_mmap(partition, 0, partition->size, SPI_FLASH_MMAP_DATA, &root,
                           (spi_flash_mmap_handle_t *)models->mmap_handle) != ESP_OK) {
        free(models->mmap_handle);
        models->mmap_handle = NULL;
        free(models);
        static_srmodels = NULL;
        return NULL;
    }
#endif
    models->partition = (esp_partition_t *)partition;
    srmodel_load(root);
    return models;
}

void srmodel_mmap_deinit(srmodel_list_t *models) {
    if (models != NULL) {
        if (models->mmap_handle) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
            esp_partition_munmap(*(esp_partition_mmap_handle_t *)models->mmap_handle);
#else
            spi_flash_munmap(*(spi_flash_mmap_handle_t *)models->mmap_handle);
#endif
            free(models->mmap_handle);
            models->mmap_handle = NULL;
        }

        if (models->num > 0 && models->model_data != NULL) {
            for (int i = 0; i < models->num; i++) {
                if (models->model_data[i]) {
                    free(models->model_data[i]->files);
                    free(models->model_data[i]->data);
                    free(models->model_data[i]->sizes);
                    free(models->model_data[i]);
                }
                if (models->model_name && models->model_name[i]) {
                    free(models->model_name[i]);
                }
                if (models->model_info && models->model_info[i] != NULL) {
                    free(models->model_info[i]);
                }
            }
        }
        free(models->model_data);
        free(models->model_name);
        free(models->model_info);
        free(models);
    }
    static_srmodels = NULL;
    srmodel_init_refcount = 0;
}

srmodel_list_t *esp_srmodel_init(const char *partition_label) {
    srmodel_init_refcount++;
    const esp_partition_t *partition = NULL;
    if (partition_label != NULL) {
        partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partition_label);
    }
    if (partition != NULL) {
        ESP_LOGI(TAG, "Chargement srmodels depuis la partition Flash '%s'", partition_label);
        return srmodel_mmap_init(partition);
    }
    if (voicekit_srmodels_bonjouresp_bin_len > 0) {
        return srmodel_load((const void *)voicekit_srmodels_bonjouresp_bin);
    }
    return NULL;
}

void esp_srmodel_deinit(srmodel_list_t *models) {
    srmodel_mmap_deinit(models);
}

void esp_srmodel_deinit_with_refcount(srmodel_list_t *models) {
    if (srmodel_init_refcount <= 1) {
        esp_srmodel_deinit(models);
    } else {
        srmodel_init_refcount--;
    }
}

static char *esp_strstr(const char *haystack, const char *needle) {
    if (needle == NULL) {
        return (char *)haystack;
    }
    return (char *)strstr(haystack, needle);
}

char *esp_srmodel_filter(srmodel_list_t *models, const char *keyword1, const char *keyword2) {
    if (models == NULL) {
        return NULL;
    }
    for (int i = 0; i < models->num; i++) {
        if (esp_strstr(models->model_name[i], keyword1) != NULL) {
            if (esp_strstr(models->model_name[i], keyword2) != NULL) {
                return models->model_name[i];
            }
        }
    }
    return NULL;
}

int esp_srmodel_exists(srmodel_list_t *models, char *model_name) {
    if (models == NULL || model_name == NULL) {
        return -1;
    }
    for (int i = 0; i < models->num; i++) {
        if (strcmp(models->model_name[i], model_name) == 0) {
            return i;
        }
    }
    return -1;
}

char *esp_srmodel_get_wake_words(srmodel_list_t *models, char *model_name) {
    if (models == NULL || model_name == NULL) {
        return NULL;
    }
    for (int i = 0; i < models->num; i++) {
        if (strcmp(models->model_name[i], model_name) == 0) {
            return get_wake_words_from_info(models->model_info[i]);
        }
    }
    return NULL;
}

void voicekit_wakenet_clean(const esp_wn_iface_t *wakenet, model_iface_data_t *model_data) {
    if (!wakenet || !model_data) {
        return;
    }
    if (wakenet->clean) {
        wakenet->clean(model_data);
    }
    if (wakenet == &esp_sr_wakenet9l_quantized) {
        // Réinitialisation complète des files convq supplémentaires (offset 36 / 60)
        // et des compteurs de sourdine post-déclenchement (offsets 120 et 136)
        uint8_t *raw = (uint8_t *)model_data;
        dl_convq_queue_t *cq_extra = *(dl_convq_queue_t **)(raw + 36);
        if (cq_extra) {
            dl_convq_queue_bzero(cq_extra);
        }
        int32_t has_verify = *(int32_t *)(raw + 48);
        if (has_verify) {
            dl_convq_queue_t *cq_verify = *(dl_convq_queue_t **)(raw + 60);
            if (cq_verify) {
                dl_convq_queue_bzero(cq_verify);
            }
        }
        *(int32_t *)(raw + 120) = 0;
        *(int32_t *)(raw + 136) = 0;
    }
}

#endif // defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)

