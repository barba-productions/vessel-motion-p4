/*
 * Ethernet bring-up for the Waveshare ESP32-P4-NANO.
 * IP101GRI PHY on RMII. Pin map per the official Waveshare wiki.
 */
#include "network.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_com.h"
#include "driver/gpio.h"
#include "settings.h"

static const char *TAG = "network";

/* Static IP for the direct LAN link to the laptop (no DHCP server present).
 * Matches the laptop's 192.168.11.177/24. */
#define ETH_STATIC_IP       "192.168.11.222"
#define ETH_STATIC_NETMASK  "255.255.255.0"
#define ETH_STATIC_GW       "0.0.0.0"

/* Waveshare ESP32-P4-NANO IP101GRI RMII pin map.
 * RXD0/RXD1 must be valid P4 EMAC iomux pins (RXD0: 29/46/52, RXD1: 30/47/53). */
#define ETH_RMII_TXD0      34
#define ETH_RMII_TXD1      35
#define ETH_RMII_RXD0      29
#define ETH_RMII_RXD1      30
#define ETH_RMII_TX_EN     49
#define ETH_RMII_CRS_DV    28
#define ETH_RMII_REF_CLK   50   /* 50 MHz from PHY-doubled 25 MHz crystal */
#define ETH_MDIO           52
#define ETH_MDC            31
#define ETH_PHY_RST        51
#define ETH_PHY_ADDR       1

static esp_netif_t *s_netif;
static bool s_connected;
static esp_ip4_addr_t s_ip;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    s_ip = event->ip_info.ip;
    s_connected = true;
    ESP_LOGI(TAG, "Ethernet up, IP: " IPSTR, IP2STR(&s_ip));
}

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        /* Link up; with a static IP we are reachable immediately, with DHCP
         * once IP_EVENT_ETH_GOT_IP arrives. */
        s_connected = true;
        ESP_LOGI(TAG, "Link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Link down");
        s_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    default:
        break;
    }
}

esp_err_t network_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&netif_cfg);
    esp_netif_set_hostname(s_netif, settings_str("hostname"));

    /* IP config from the Network settings tab (applied at boot):
     * dhcp=yes -> DHCP client; dhcp=no -> static IPv4 from ip/subnet/gateway/dns. */
    if (settings_bool("dhcp")) {
        ESP_LOGI(TAG, "Ethernet using DHCP");
    } else {
        esp_netif_dhcpc_stop(s_netif);
        esp_netif_ip_info_t ip_info = { 0 };
        if (esp_netif_str_to_ip4(settings_str("ip"), &ip_info.ip) != ESP_OK ||
            esp_netif_str_to_ip4(settings_str("subnet"), &ip_info.netmask) != ESP_OK ||
            esp_netif_str_to_ip4(settings_str("gateway"), &ip_info.gw) != ESP_OK) {
            /* never lose the link over a bad setting: fall back to the known-good address */
            ESP_LOGW(TAG, "Invalid static IPv4 settings; falling back to %s", ETH_STATIC_IP);
            esp_netif_str_to_ip4(ETH_STATIC_IP, &ip_info.ip);
            esp_netif_str_to_ip4(ETH_STATIC_NETMASK, &ip_info.netmask);
            esp_netif_str_to_ip4(ETH_STATIC_GW, &ip_info.gw);
        }
        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif, &ip_info));
        s_ip = ip_info.ip;
        esp_netif_dns_info_t dns = { 0 };
        if (esp_netif_str_to_ip4(settings_str("dns"), &dns.ip.u_addr.ip4) == ESP_OK) {
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
        }
        ESP_LOGI(TAG, "Ethernet static IP: " IPSTR, IP2STR(&ip_info.ip));
    }

    /* MAC (RMII) */
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num = ETH_MDC;
    emac_cfg.smi_gpio.mdio_num = ETH_MDIO;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = ETH_RMII_REF_CLK;
    emac_cfg.emac_dataif_gpio.rmii.tx_en_num = ETH_RMII_TX_EN;
    emac_cfg.emac_dataif_gpio.rmii.txd0_num = ETH_RMII_TXD0;
    emac_cfg.emac_dataif_gpio.rmii.txd1_num = ETH_RMII_TXD1;
    emac_cfg.emac_dataif_gpio.rmii.crs_dv_num = ETH_RMII_CRS_DV;
    emac_cfg.emac_dataif_gpio.rmii.rxd0_num = ETH_RMII_RXD0;
    emac_cfg.emac_dataif_gpio.rmii.rxd1_num = ETH_RMII_RXD1;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &(eth_mac_config_t)ETH_MAC_DEFAULT_CONFIG());

    /* PHY (IP101) */
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = ETH_PHY_RST;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle;
    esp_err_t err = esp_eth_driver_install(&eth_cfg, &eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed (%s); running without Ethernet",
                 esp_err_to_name(err));
        return err;  /* non-fatal: keep Wi-Fi/console alive */
    }

    ESP_ERROR_CHECK(esp_netif_attach(s_netif, esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    ESP_LOGI(TAG, "Ethernet driver started (IP101GRI)");
    return ESP_OK;
}

bool network_is_connected(void)
{
    return s_connected;
}

esp_err_t network_get_ip(char *buf, size_t len)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    snprintf(buf, len, IPSTR, IP2STR(&s_ip));
    return ESP_OK;
}
