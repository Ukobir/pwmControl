/* AULA IoT - Ricardo Prates - 001 - Cliente MQTT - Publisher:/Temperatura; Subscribed:/led
 *
 * Material de suporte - 27/05/2025
 *
 * Código adaptado de: https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/mqtt
 */

#include "pico/stdlib.h"     // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "pico/unique_id.h"  // Biblioteca com recursos para trabalhar com os pinos GPIO do Raspberry Pi Pico

#include "hardware/gpio.h" // Biblioteca de hardware de GPIO
#include "hardware/irq.h"  // Biblioteca de hardware de interrupções
#include "hardware/adc.h"  // Biblioteca de hardware para conversão ADC
#include "hardware/pwm.h"  // Adiciona PWM para simular o controle de motor

#include "lwip/apps/mqtt.h"      // Biblioteca LWIP MQTT -  fornece funções e recursos para conexão MQTT
#include "lwip/apps/mqtt_priv.h" // Biblioteca que fornece funções e recursos para Geração de Conexões
#include "lwip/dns.h"            // Biblioteca que fornece funções e recursos suporte DNS:
#include "lwip/altcp_tls.h"      // Biblioteca que fornece funções e recursos para conexões seguras usando TLS:

#include "lib/ws2812.h"
#include "lib/ssd1306.h"
#include "lib/func.c"

// This file includes your client certificate for client server authentication
#ifdef MQTT_CERT_INC
#include MQTT_CERT_INC
#endif

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 200
#endif

// Dados do cliente MQTT
typedef struct
{
    mqtt_client_t *mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    char data[MQTT_OUTPUT_RINGBUF_SIZE];
    char topic[MQTT_TOPIC_LEN];
    uint32_t len;
    ip_addr_t mqtt_server_address;
    bool connect_done;
    int subscribe_count;
    bool stop_client;
} MQTT_CLIENT_DATA_T;

#ifndef DEBUG_printf
#ifndef NDEBUG
#define DEBUG_printf printf
#else
#define DEBUG_printf(...)
#endif
#endif

#ifndef INFO_printf
#define INFO_printf printf
#endif

#ifndef ERROR_printf
#define ERROR_printf printf
#endif

// Manter o programa ativo - keep alive in seconds
#define MQTT_KEEP_ALIVE_S 60

// QoS - mqtt_subscribe
// At most once (QoS 0)
// At least once (QoS 1)
// Exactly once (QoS 2)
#define MQTT_SUBSCRIBE_QOS 1
#define MQTT_PUBLISH_QOS 1
#define MQTT_PUBLISH_RETAIN 0

// Tópico usado para: last will and testament
#define MQTT_WILL_TOPIC "/online"
#define MQTT_WILL_MSG "0"
#define MQTT_WILL_QOS 1

#ifndef MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "pico"
#endif

// Definir como 1 para adicionar o nome do cliente aos tópicos, para suportar vários dispositivos que utilizam o mesmo servidor
#ifndef MQTT_UNIQUE_TOPIC
#define MQTT_UNIQUE_TOPIC 0
#endif

#define PICO_CLOCK_FREQ_HZ 125000000
#define CREDENTIAL_BUFFER_SIZE 64 // Tamanho do buffer para armazenar as credenciais

// Add these constants at the top
#define LED_MATRIX_SIZE 5
#define LED_GREEN_START 24
#define LED_RED_START 4
#define LED_BLUE_START 14
#define DUTY_CYCLE_DIVISOR 20
#define RGB_LED_COUNT 3
#define PWM_ARRAY_OFFSET 11

// Requisição para publicar
static void pub_request_cb(__unused void *arg, err_t err);

// Topico MQTT
static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name);

// Requisição de Assinatura - subscribe
static void sub_request_cb(void *arg, err_t err);

// Requisição para encerrar a assinatura
static void unsub_request_cb(void *arg, err_t err);

// Tópicos de assinatura
static void sub_unsub_topics(MQTT_CLIENT_DATA_T *state, bool sub);

// Dados de entrada MQTT
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);

// Dados de entrada publicados
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);

// Conexão MQTT
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);

// Inicializar o cliente MQTT
static void start_client(MQTT_CLIENT_DATA_T *state);

// Call back com o resultado do DNS
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg);

const uint led_rgb[RGB_LED_COUNT] = {11, 12, 13};
static uint16_t pwm_wraps[RGB_LED_COUNT] = {0};
static uint32_t fpwm[RGB_LED_COUNT] = {0};
// Funções para o controle do PWM ===============================
void setup_pwm(uint gpio, uint8_t div)
{
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, div);
    pwm_set_wrap(slice_num, pwm_wraps[gpio - PWM_ARRAY_OFFSET]);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(gpio, 0); // Inicializa o PWM no nível baixo
}

static void set_pwm_duty(uint gpio, uint duty_cycle_percent)
{
    uint16_t level = (pwm_wraps[gpio - PWM_ARRAY_OFFSET] * duty_cycle_percent) / 100;
    pwm_set_gpio_level(gpio, level);
}
// Fim das funções para o controle do PWM ===============================

// Função para desenhar na matriz de LEDs ===============================
void draw_matrix_green(uint8_t duty)
{
    for (int i = 0; i < LED_MATRIX_SIZE; i++)
    {
        int j = LED_GREEN_START - i; // Perccorre do led 24 ao led 20
        if (duty - i > 0)
        {
            npSetLED(j, 0, 1, 0);
        }
        else
        {
            npSetLED(j, 0, 0, 0);
        }
    }
    npWrite();
}

void draw_matrix_red(uint8_t duty)
{
    for (int i = 0; i < LED_MATRIX_SIZE; i++)
    {
        int j = LED_RED_START - i; // Perccorre do led 4 ao led 0
        if (duty - i > 0)
        {
            npSetLED(j, 1, 0, 0);
        }
        else
        {
            npSetLED(j, 0, 0, 0);
        }
    }
    npWrite();
}

void draw_matrix_blue(uint8_t duty)
{
    for (int i = 0; i < LED_MATRIX_SIZE; i++)
    {
        int j = LED_BLUE_START - i; // Perccorre do led 14 ao led 10
        if (duty - i > 0)
        {
            npSetLED(j, 0, 0, 1);
        }
        else
        {
            npSetLED(j, 0, 0, 0);
        }
    }
    npWrite();
}

// Variável para o controle do display ===============================
ssd1306_t ssd;

// Credenciais  da rede Wi-Fi e MQTT ===============================
char WIFI_SSID[CREDENTIAL_BUFFER_SIZE];     // Substitua pelo nome da sua rede Wi-Fi
char WIFI_PASSWORD[CREDENTIAL_BUFFER_SIZE]; // Substitua pela senha da sua rede Wi-Fi
char MQTT_SERVER[CREDENTIAL_BUFFER_SIZE];   // Substitua pelo endereço do host - broket MQTT: Ex: 192.168.1.107
char MQTT_USERNAME[CREDENTIAL_BUFFER_SIZE]; // Substitua pelo nome da host MQTT - admin
char MQTT_PASSWORD[CREDENTIAL_BUFFER_SIZE]; // Substitua pelo Password da host MQTT - admin

int main(void)
{

    // Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();
    INFO_printf("mqtt client starting\n");

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    // Inicializa a matriz de LEDs
    npInit();
    npClear();
    npWrite();

    // Inicializa o display
    initDisplay(&ssd);

    draw_opening_usb(&ssd); // Desenha a tela de espera da comunicação USB
    waitUSB();              // Espera a comunicação USB

    wifi_Credentials(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_USERNAME, MQTT_PASSWORD); // Solicita as credenciais da rede Wi-Fi
    draw_opening_screen(&ssd);                                                             // Desenha a tela de espera da conexão com a rede Wi-Fi

    // Cria registro com os dados do cliente
    static MQTT_CLIENT_DATA_T state;

    // Inicializa a arquitetura do cyw43
    if (cyw43_arch_init())
    {
        panic("Failed to inizialize CYW43");
    }

    // Usa identificador único da placa
    char unique_id_buf[5];
    pico_get_unique_board_id_string(unique_id_buf, sizeof(unique_id_buf));
    for (int i = 0; i < sizeof(unique_id_buf) - 1; i++)
    {
        unique_id_buf[i] = tolower(unique_id_buf[i]);
    }

    // Gera nome único, Ex: pico1234
    char client_id_buf[sizeof(MQTT_DEVICE_NAME) + sizeof(unique_id_buf) - 1];
    memcpy(&client_id_buf[0], MQTT_DEVICE_NAME, sizeof(MQTT_DEVICE_NAME) - 1);
    memcpy(&client_id_buf[sizeof(MQTT_DEVICE_NAME) - 1], unique_id_buf, sizeof(unique_id_buf) - 1);
    client_id_buf[sizeof(client_id_buf) - 1] = 0;
    INFO_printf("Device name %s\n", client_id_buf);

    state.mqtt_client_info.client_id = client_id_buf;
    state.mqtt_client_info.keep_alive = MQTT_KEEP_ALIVE_S; // Keep alive in sec
    state.mqtt_client_info.client_user = MQTT_USERNAME;
    state.mqtt_client_info.client_pass = MQTT_PASSWORD;
    static char will_topic[MQTT_TOPIC_LEN];
    strncpy(will_topic, full_topic(&state, MQTT_WILL_TOPIC), sizeof(will_topic));
    state.mqtt_client_info.will_topic = will_topic;
    state.mqtt_client_info.will_msg = MQTT_WILL_MSG;
    state.mqtt_client_info.will_qos = MQTT_WILL_QOS;
    state.mqtt_client_info.will_retain = true;
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    // TLS enabled
#ifdef MQTT_CERT_INC
    static const uint8_t ca_cert[] = TLS_ROOT_CERT;
    static const uint8_t client_key[] = TLS_CLIENT_KEY;
    static const uint8_t client_cert[] = TLS_CLIENT_CERT;
    // This confirms the indentity of the server and the client
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client_2wayauth(ca_cert, sizeof(ca_cert),
                                                                                client_key, sizeof(client_key), NULL, 0, client_cert, sizeof(client_cert));
#if ALTCP_MBEDTLS_AUTHMODE != MBEDTLS_SSL_VERIFY_REQUIRED
    WARN_printf("Warning: tls without verification is insecure\n");
#endif
#else
    state->client_info.tls_config = altcp_tls_create_config_client(NULL, 0);
    WARN_printf("Warning: tls without a certificate is insecure\n");
#endif
#endif

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        panic("Failed to connect");
    }
    INFO_printf("\nConnected to Wifi\n");

    // Faz um pedido de DNS para o endereço IP do servidor MQTT
    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(MQTT_SERVER, &state.mqtt_server_address, dns_found, &state);
    cyw43_arch_lwip_end();

    // Se tiver o endereço, inicia o cliente
    if (err == ERR_OK)
    {
        start_client(&state);
    }
    else if (err != ERR_INPROGRESS)
    { // ERR_INPROGRESS means expect a callback
        panic("dns request failed");
    }

    // Loop condicionado a conexão mqtt
    while (!state.connect_done || mqtt_client_is_connected(state.mqtt_client_inst))
    {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10000));
    }

    INFO_printf("mqtt client exiting\n");
    return 0;
}

// Requisição para publicar
static void pub_request_cb(__unused void *arg, err_t err)
{
    if (err != 0)
    {
        ERROR_printf("pub_request_cb failed %d", err);
    }
}

// Topico MQTT
static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name)
{
#if MQTT_UNIQUE_TOPIC
    static char full_topic[MQTT_TOPIC_LEN];
    snprintf(full_topic, sizeof(full_topic), "/%s%s", state->mqtt_client_info.client_id, name);
    return full_topic;
#else
    return name;
#endif
}

// Requisição de Assinatura - subscribe
static void sub_request_cb(void *arg, err_t err)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (err != 0)
    {
        panic("subscribe request failed %d", err);
    }
    state->subscribe_count++;
}

// Requisição para encerrar a assinatura
static void unsub_request_cb(void *arg, err_t err)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (err != 0)
    {
        panic("unsubscribe request failed %d", err);
    }
    state->subscribe_count--;
    assert(state->subscribe_count >= 0);

    // Stop if requested
    if (state->subscribe_count <= 0 && state->stop_client)
    {
        mqtt_disconnect(state->mqtt_client_inst);
    }
}

// Tópicos de assinatura
static void sub_unsub_topics(MQTT_CLIENT_DATA_T *state, bool sub)
{
    mqtt_request_cb_t cb = sub ? sub_request_cb : unsub_request_cb;
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/exit"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    // Estes são os tópicos de assinatura do texto input no MQTT Panel (div: 8bits, wrap: 16bits)
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/pwmg"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/pwmb"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/pwmr"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    // Estes são os tópicos de assinatura do slider no MQTT Panel (duty cycle: 0-100%)
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/spwmg"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/spwmb"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/spwmr"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
}

// Dados de entrada MQTT
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
#if MQTT_UNIQUE_TOPIC
    const char *basic_topic = state->topic + strlen(state->mqtt_client_info.client_id) + 1;
#else
    const char *basic_topic = state->topic;
#endif
    strncpy(state->data, (const char *)data, len);
    state->len = len;
    state->data[len] = '\0';

    DEBUG_printf("Topic: %s, Message: %s\n", state->topic, state->data);

    if (strcmp(basic_topic, "/exit") == 0)
    {
        state->stop_client = true;      // stop the client when ALL subscriptions are stopped
        sub_unsub_topics(state, false); // unsubscribe
    }
    else if (strcmp(basic_topic, "/spwmg") == 0)
    {
        // Espera uma string no formato "div,wrap"
        uint8_t div;
        uint16_t wrap;
        if (sscanf(state->data, "%hhu,%u", &div, &wrap) == 2)
        {
            if (div > 255)
            {
                ERROR_printf("Divisor invalido\n");
                div = 255;
            }
            else if (wrap > 65535)
            {
                ERROR_printf("Wrap invalido\n");
                wrap = 65535;
            }
            pwm_wraps[0] = wrap;
            fpwm[0] = PICO_CLOCK_FREQ_HZ / (wrap * div);
            setup_pwm(led_rgb[0], div); // Configura o pwm para o led verde
            draw_sucess_screen(&ssd, 1);
            INFO_printf("Configurou o pwm para div:%hhu wrap:%u\n", div, wrap);
            INFO_printf("E frequência de:%u Hz\n", fpwm[0]);
        }
        else
        {
            ERROR_printf("Formato invalido. Esperado div,wrap\n");
        }
    }
    else if (strcmp(basic_topic, "/spwmb") == 0)
    {
        // Espera uma string no formato "div,wrap"
        uint8_t div;
        uint16_t wrap;
        if (sscanf(state->data, "%hhu,%u", &div, &wrap) == 2)
        {
            if (div > 255)
            {
                ERROR_printf("Divisor invalido\n");
                div = 255;
            }
            else if (wrap > 65535)
            {
                ERROR_printf("Wrap invalido\n");
                wrap = 65535;
            }
            pwm_wraps[1] = wrap;
            fpwm[1] = PICO_CLOCK_FREQ_HZ / (wrap * div);
            setup_pwm(led_rgb[1], div); // Configura o pwm para o led verde
            draw_sucess_screen(&ssd, 2);
            INFO_printf("Configurou o pwm para div:%hhu wrap:%u\n", div, wrap);
            INFO_printf("E frequência de:%u Hz\n", fpwm[1]);
        }
        else
        {
            ERROR_printf("Formato invalido. Esperado div,wrap\n");
        }
    }
    else if (strcmp(basic_topic, "/spwmr") == 0)
    {
        // Espera uma string no formato "div,wrap"

        uint8_t div;
        uint16_t wrap;
        if (sscanf(state->data, "%hhu,%u", &div, &wrap) == 2)
        {
            if (div > 255)
            {
                ERROR_printf("Divisor invalido\n");
                div = 255;
            }
            else if (wrap > 65535)
            {
                ERROR_printf("Wrap invalido\n");
                wrap = 65535;
            }
            pwm_wraps[2] = wrap;
            fpwm[2] = PICO_CLOCK_FREQ_HZ / (wrap * div);
            setup_pwm(led_rgb[2], div); // Configura o pwm para o led verde
            draw_sucess_screen(&ssd, 3);
            INFO_printf("Configurou o pwm para div:%hhu wrap:%u\n", div, wrap);
            INFO_printf("E frequência de:%u Hz\n", fpwm[2]);
        }
        else
        {
            ERROR_printf("Formato invalido. Esperado div,wrap\n");
        }
    }
    else if (strcmp(basic_topic, "/pwmg") == 0)
    {
        // Espera o duty cyclo do pwm 1
        uint duty;
        if (sscanf(state->data, "%u", &duty) == 1)
        {
            draw_matrix_green(duty / DUTY_CYCLE_DIVISOR);
            set_pwm_duty(led_rgb[0], duty);
            draw_pwm_config(&ssd, fpwm[0], duty, 1);
            INFO_printf("Ligou o Led Verde no valor de: %u%%\n", duty);
        }
        else
        {
            ERROR_printf("Erro formato invalido. Esperado 0-100\n");
        }
    }
    else if (strcmp(basic_topic, "/pwmb") == 0)
    {
        // Espera o duty cyclo do pwm 2
        uint duty;
        if (sscanf(state->data, "%u", &duty) == 1)
        {
            draw_matrix_blue(duty / DUTY_CYCLE_DIVISOR);
            set_pwm_duty(led_rgb[1], duty);
            draw_pwm_config(&ssd, fpwm[1], duty, 2);
            INFO_printf("Ligou o Led AZUL no valor de: %u%%\n", duty);
        }
        else
        {
            ERROR_printf("Erro formato invalido. Esperado 0-100\n");
        }
    }
    else if (strcmp(basic_topic, "/pwmr") == 0)
    {
        // Espera o duty cyclo do pwm 3
        uint duty;
        if (sscanf(state->data, "%u", &duty) == 1)
        {
            draw_matrix_red(duty / DUTY_CYCLE_DIVISOR);
            set_pwm_duty(led_rgb[2], duty);
            draw_pwm_config(&ssd, fpwm[2], duty, 3);
            INFO_printf("Ligou o Led Vermelho no valor de: %u%%\n", duty);
        }
        else
        {
            ERROR_printf("Erro formato invalido. Esperado 0-100\n");
        }
    }
}

// Dados de entrada publicados
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    // Safer approach:
    strncpy(state->topic, topic, sizeof(state->topic) - 1);
    state->topic[sizeof(state->topic) - 1] = '\0';
    ;
}

// Conexão MQTT
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        state->connect_done = true;
        sub_unsub_topics(state, true); // subscribe;

        // indicate online
        if (state->mqtt_client_info.will_topic)
        {
            mqtt_publish(state->mqtt_client_inst, state->mqtt_client_info.will_topic, "1", 1, MQTT_WILL_QOS, true, pub_request_cb, state);
        }

        INFO_printf("Connected to MQTT server\n");
    }
    else if (status == MQTT_CONNECT_DISCONNECTED)
    {
        if (!state->connect_done)
        {
            panic("Failed to connect to mqtt server");
        }
    }
    else
    {
        panic("Unexpected status");
    }
}

// Inicializar o cliente MQTT
static void start_client(MQTT_CLIENT_DATA_T *state)
{
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    const int port = MQTT_TLS_PORT;
    INFO_printf("Using TLS\n");
#else
    const int port = MQTT_PORT;
    INFO_printf("Warning: Not using TLS\n");
#endif

    state->mqtt_client_inst = mqtt_client_new();
    if (!state->mqtt_client_inst)
    {
        panic("MQTT client instance creation error");
    }
    INFO_printf("IP address of this device %s\n", ipaddr_ntoa(&(netif_list->ip_addr)));
    INFO_printf("Connecting to mqtt server at %s\n", ipaddr_ntoa(&state->mqtt_server_address));

    cyw43_arch_lwip_begin();
    if (mqtt_client_connect(state->mqtt_client_inst, &state->mqtt_server_address, port, mqtt_connection_cb, state, &state->mqtt_client_info) != ERR_OK)
    {
        panic("MQTT broker connection error");
    }
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    // This is important for MBEDTLS_SSL_SERVER_NAME_INDICATION
    mbedtls_ssl_set_hostname(altcp_tls_context(state->mqtt_client_inst->conn), MQTT_SERVER);
#endif
    mqtt_set_inpub_callback(state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, state);
    cyw43_arch_lwip_end();
}

// Call back com o resultado do DNS
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (ipaddr)
    {
        state->mqtt_server_address = *ipaddr;
        start_client(state);
    }
    else
    {
        panic("dns request failed");
    }
}
