// O HEAP DA PSRAM NO BOOT - ver tab5_heap_psram.hpp.

#include "tab5_heap_psram.hpp"

#include <stddef.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "riscv/rv_utils.h"   // rv_utils_set_watchpoint: o watchpoint direto no hardware
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/startup_internal.h"   // ESP_SYSTEM_INIT_FN

static const char *TAG = "heap_psram";

// A GUARDA. O IDF monta o heap da PSRAM a partir do FIM das variaveis
// estaticas que moram nela (.ext_ram.bss) - sem alinhar a linha de cache (128
// bytes no P4). Esta guarda (secao noinit, que o IDF poe DEPOIS da bss, e
// CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY no sdkconfig.defaults) faz o
// heap comecar numa linha de cache so dele. Mitigacao, NAO a causa: um build
// so com codigo do Wi-Fi mudado voltou a cair todo boot com ela no lugar.
EXT_RAM_NOINIT_ATTR __attribute__((aligned(128), used)) static uint8_t s_guarda[128];

// O cabecalho do heap e o heap_t de components/heap/multi_heap.c, que o IDF
// grava no inicio da regiao - aqui, logo depois da guarda.
struct Cabecalho {
    void*  lock;      // gravado uma vez (multi_heap_set_lock) e nunca mais
    size_t livre;     // free_bytes: muda a cada malloc/free
    size_t minimo;    // minimum_free_bytes: idem
    size_t tamanho;   // pool_size: fixo
    void*  dados;     // heap_data, o controle do TLSF: fixo (cabecalho + 20)
};
extern "C" uint8_t _ext_ram_noinit_end;
static Cabecalho* cabecalho() { return (Cabecalho*)&_ext_ram_noinit_end; }

// Copia na RAM interna, tirada logo depois da montagem. dados == nullptr: o
// heap nao esta onde esperamos e nem sentinela nem checagem valem.
static Cabecalho s_retrato;

// Sobrevive ao esp_restart (RTC_NOINIT). [0] = magia, [1] = reinicios limpos
// seguidos feitos pela checagem.
static constexpr uint32_t MAGIA = 0x48505352;   // "HPSR"
RTC_NOINIT_ATTR static uint32_t s_reinicios[2];
static uint32_t s_reinicios_antes;

// A SENTINELA. lock e heap_data sao gravados na montagem e nunca mais: um
// watchpoint de escrita neles faz a CPU parar NA INSTRUCAO que os estragar
// (panico "Debug exception" com o PC, no core dump). Vale por nucleo, por isso
// e armada nos dois. Slots 0 e 1 (o FreeRTOS nao usa mais o 2: ver o
// WATCHPOINT_END_OF_STACK no sdkconfig.defaults, 06/11/2026). Se o
// cabecalho estragar sem ela disparar, nao foi a CPU: DMA, cache ou a PSRAM.
static void armar(void)
{
    if (s_retrato.dados == nullptr) {
        return;
    }
    // DIRETO NO HARDWARE, nao por esp_cpu_set_watchpoint(): aquele pergunta se
    // ha depurador (assist_debug_ll_is_debugger_active) e, com o bit ligado,
    // usa semihosting - um ebreak que, sem depurador atendendo, e panico. O
    // bit fica ligado depois do OpenOCD (e sobrevive ao reset): virava panico
    // no boot, em laco (06/11/2026). Sem depurador o IDF faz exatamente isto.
    rv_utils_set_watchpoint(0, (uint32_t)&cabecalho()->dados, 4, false, true);
    rv_utils_set_watchpoint(1, (uint32_t)&cabecalho()->lock, 4, false, true);
}

// Nucleo 0: logo depois de add_psram_to_heap (103) e ANTES dos construtores
// estaticos (do_global_ctors), que a sentinela antiga, armada no app_main, nao
// cobria.
ESP_SYSTEM_INIT_FN(heap_psram_sentinela_n0, CORE, BIT(0), 104)
{
    const Cabecalho* c = cabecalho();
    if (c->dados == (uint8_t*)c + sizeof(Cabecalho) && c->lock != nullptr) {
        s_retrato = *c;
    }
    armar();
    return ESP_OK;
}

#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
// Nucleo 1: so roda codigo a partir da etapa secundaria, entao armar aqui
// cobre tudo o que ele faz.
ESP_SYSTEM_INIT_FN(heap_psram_sentinela_n1, SECONDARY, BIT(1), 100)
{
    armar();
    return ESP_OK;
}
#endif

void tab5_heap_psram_conferir(void)
{
    { static void* volatile g = s_guarda; (void)g; }   // sem uso, o linker jogaria a guarda fora

    if (s_reinicios[0] != MAGIA) {   // ligado agora: o RTC_NOINIT vem com lixo
        s_reinicios[0] = MAGIA;
        s_reinicios[1] = 0;
    }
    s_reinicios_antes = s_reinicios[1];

    if (s_retrato.dados == nullptr) {
        ESP_LOGW(TAG, "heap da PSRAM fora do lugar esperado - sem sentinela nem checagem");
        return;
    }
    const Cabecalho* c = cabecalho();
    const Cabecalho  v = *c;   // uma leitura so; reler pode mascarar (ja sumiu antes com checagens)
    if (v.dados == s_retrato.dados && v.lock == s_retrato.lock &&
        v.tamanho == s_retrato.tamanho && v.livre <= v.tamanho && v.minimo <= v.livre) {
        s_reinicios[1] = 0;
        return;
    }

    ESP_EARLY_LOGE(TAG, "CABECALHO ESTRAGADO em %p: lock %p/%p livre 0x%x minimo 0x%x tamanho 0x%x/0x%x dados %p/%p",
                   c, v.lock, s_retrato.lock, (unsigned)v.livre, (unsigned)v.minimo,
                   (unsigned)v.tamanho, (unsigned)s_retrato.tamanho, v.dados, s_retrato.dados);
    if (s_reinicios[1] >= 3) {
        ESP_EARLY_LOGE(TAG, "ja reiniciei %lu vezes seguidas - seguindo (vai dar panico)",
                       (unsigned long)s_reinicios[1]);
        s_reinicios[1] = 0;
        return;
    }
    s_reinicios[1]++;
    esp_restart();
}

uint32_t tab5_heap_psram_reinicios(void)
{
    return s_reinicios_antes;
}

// O FLAGRANTE. A checagem do app_main nunca pegou nada: o cabecalho estraga
// DENTRO do M5.begin, entre o primeiro e o segundo quadro de 1,8 MB que o
// esp_lcd reserva (num_fbs = 2 no Panel_DSI). Esta olha a linha de cache
// inteira do cabecalho nos pontos de suspeita - antes de cada reserva grande
// na PSRAM e depois de cada sincronia grande de cache. Estragada: mostra a
// linha como a CPU a ve, INVALIDA a linha (descarta o que o cache tem) e
// mostra de novo. Se a segunda leitura sair certa, a PSRAM estava boa e o erro
// era do cache; se sair igual, o dado errado ja estava na PSRAM.
#include "esp_rom_sys.h"
#include "esp_cache.h"
extern "C" esp_err_t __real_esp_cache_msync(void* addr, size_t size, int flags);

static DRAM_ATTR uint32_t s_flagrantes = 0;

static void IRAM_ATTR mostrar_linha(const char* rotulo, const uint32_t* w)
{
    esp_rom_printf("heap_psram: %s\n", rotulo);
    for (int i = 0; i < 32; i += 8) {
        esp_rom_printf("  +%02x: %08x %08x %08x %08x %08x %08x %08x %08x\n", i * 4,
                       w[i], w[i + 1], w[i + 2], w[i + 3], w[i + 4], w[i + 5], w[i + 6], w[i + 7]);
    }
}

extern "C" void IRAM_ATTR tab5_heap_psram_checar(const char* onde, size_t tam)
{
    if (s_retrato.dados == nullptr) {
        return;
    }
    volatile uint32_t* linha = (volatile uint32_t*)cabecalho();
    uint32_t w[32];
    for (int i = 0; i < 32; ++i) { w[i] = linha[i]; }
    const Cabecalho* v = (const Cabecalho*)w;
    if (v->dados == s_retrato.dados && v->lock == s_retrato.lock && v->tamanho == s_retrato.tamanho) {
        return;
    }
    ++s_flagrantes;
    esp_rom_printf("heap_psram: FLAGRANTE #%u %s (%u bytes): esperado lock %p tamanho 0x%x dados %p\n",
                   (unsigned)s_flagrantes, onde, (unsigned)tam, s_retrato.lock,
                   (unsigned)s_retrato.tamanho, s_retrato.dados);
    mostrar_linha("linha como a CPU ve", w);
    __real_esp_cache_msync((void*)linha, 128, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    for (int i = 0; i < 32; ++i) { w[i] = linha[i]; }
    mostrar_linha("depois de invalidar (o que a PSRAM tem)", w);
    // Reinicia limpo em vez do panico (a mesma conta de reinicios do app_main).
    if (s_reinicios[0] == MAGIA && s_reinicios[1] < 3) {
        s_reinicios[1]++;
        esp_restart();
    }
}

// Toda reserva da PSRAM de 1 MB ou mais passa por aqui (--wrap no
// main/CMakeLists.txt): confere o cabecalho ANTES de o TLSF le-lo.
extern "C" void* __real_heap_caps_calloc(size_t n, size_t size, uint32_t caps);

extern "C" void* IRAM_ATTR __wrap_heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    if ((caps & MALLOC_CAP_SPIRAM) && n * size >= (1u << 20)) {
        tab5_heap_psram_checar("antes do calloc", n * size);
    }
    return __real_heap_caps_calloc(n, size, caps);
}

// O ESTRESSE ('mem estresse' no console): o mecanismo do boot sem depender do
// leiaute do binario. Uma linha de cache na PSRAM com 32 palavras conhecidas;
// a cada volta grava a palavra 1 (como o free_bytes do heap a cada malloc),
// zera 1,8 MB (como o calloc do quadro, que varre os 256 KB do L2), sincroniza
// (como o esp_lcd) e confere a linha inteira numa leitura so. Qualquer palavra
// fora do lugar e o defeito do boot acontecendo aqui, medivel.
#include <string.h>
#include "esp_timer.h"

void tab5_heap_psram_estresse(int voltas, bool zerar)
{
    const size_t TAM = 720 * 1280 * 2;
    uint32_t* linha = (uint32_t*)heap_caps_aligned_alloc(128, 128, MALLOC_CAP_SPIRAM);
    uint8_t*  grande = zerar ? (uint8_t*)heap_caps_aligned_alloc(128, TAM, MALLOC_CAP_SPIRAM) : nullptr;
    if (!linha || (zerar && !grande)) {
        printf("sem PSRAM livre para o teste\n");
        heap_caps_free(linha);
        heap_caps_free(grande);
        return;
    }
    for (int k = 0; k < 32; ++k) { linha[k] = 0xA5000000u | (uint32_t)k; }
    const int64_t t0 = esp_timer_get_time();
    int erros = 0, voltas_com_erro = 0, mostradas = 0;
    for (int i = 0; i < voltas; ++i) {
        linha[1] = (uint32_t)i;
        linha[2] = (uint32_t)i ^ 0x5555u;
        if (zerar) {
            memset(grande, i, TAM);
            __real_esp_cache_msync(grande, TAM, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
        uint32_t w[32];
        for (int k = 0; k < 32; ++k) { w[k] = ((volatile uint32_t*)linha)[k]; }
        int e = 0;
        for (int k = 0; k < 32; ++k) {
            const uint32_t esperado = k == 1 ? (uint32_t)i : k == 2 ? ((uint32_t)i ^ 0x5555u) : (0xA5000000u | (uint32_t)k);
            if (w[k] != esperado) { ++e; }
        }
        if (e) {
            erros += e;
            ++voltas_com_erro;
            if (mostradas < 4) {
                ++mostradas;
                printf("volta %d: %d palavra(s) fora do lugar (linha %p)\n", i, e, linha);
                for (int k = 0; k < 32; k += 8) {
                    printf("  +%02x: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", k * 4,
                           (unsigned long)w[k], (unsigned long)w[k + 1], (unsigned long)w[k + 2], (unsigned long)w[k + 3],
                           (unsigned long)w[k + 4], (unsigned long)w[k + 5], (unsigned long)w[k + 6], (unsigned long)w[k + 7]);
                }
            }
            for (int k = 0; k < 32; ++k) {   // repoe para a proxima volta
                linha[k] = k == 1 ? (uint32_t)i : k == 2 ? ((uint32_t)i ^ 0x5555u) : (0xA5000000u | (uint32_t)k);
            }
        }
        if ((i & 63) == 63) { vTaskDelay(1); }   // deixa o resto do sistema respirar
    }
    printf("estresse: %d voltas (%s) em %lld ms - %d com erro, %d palavras erradas\n",
           voltas, zerar ? "zerando 1,8 MB" : "so a linha",
           (long long)((esp_timer_get_time() - t0) / 1000), voltas_com_erro, erros);
    heap_caps_free(linha);
    heap_caps_free(grande);
}
