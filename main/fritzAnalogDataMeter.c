/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include <driver/dac.h>

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"



/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER 		"fritz.box"
#define WEB_PORT 		"49000"
#define MAX_INTERNET_BANDWIDTH	100000

static const char *TAG = "example";

static const char *REQUEST = "POST /igdupnp/control/WANCommonIFC1 HTTP/1.1\n"
                             "Host: 10.0.1.1:49000\n"
                             "SOAPAction: urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1#GetTotalBytesReceived\n"
                             "Content-Type: text/xml\n"
                             "User-Agent: PostmanRuntime/7.19.0\n"
                             "Accept: */*\n"
                             "Cache-Control: no-cache\n"
                             "Postman-Token: 24ce3ef3-4431-4a9e-9656-bbab9ce34839,8aca5f9c-bded-4b76-acb0-54146594b3b4\n"
                             "Host: 10.0.1.1:49000\n"
                             "Accept-Encoding: gzip, deflate\n"
                             "Content-Length: 436\n"
                             "Connection: close\n"
                             "cache-control: no-cache\n"
                             "\n"
                             "    <?xml\n"
                             "        version=\"1.0\"\n"
                             "        encoding=\"utf-8\"\n"
                             "        ?>\n"
                             "    <s:Envelope\n"
                             "        s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"\n"
                             "        xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
                             "        <s:Body>\n"
                             "            <u:GetTotalBytesReceived\n"
                             "                xmlns:u=\"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1\">\n"
                             "                </u:GetTotalBytesReceived>\n"
                             "            </s:Body>\n"
                             "        </s:Envelope>";

uint32_t bandwidth = 0;
uint32_t totalBytesReceived = 0;

void getValue() {

    const struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[600];

    // for bitrate calculation
    TickType_t lastInvocation = xTaskGetTickCount();

    bool receive = true;
    while (receive) {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if (err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if (s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                       sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        bzero(recv_buf, sizeof(recv_buf));
        do {
            r = read(s, recv_buf, sizeof(recv_buf) - 1);
            /*
            for (int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }*/
        } while (r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);

        ESP_LOGI(TAG, "Starting again!");

        // extracting number of bytes manually from char array
        char totalBytesArray[11];
        bzero(totalBytesArray, sizeof(totalBytesArray));
        int counter = 0;
        for (int i = 501; i < 510; i++) {

            totalBytesArray[counter] = recv_buf[i];
            //ESP_LOGI(TAG, "recv_buf: %c", recv_buf[i]);
            ESP_LOGI(TAG, "totalBytesArray: %c", totalBytesArray[counter]);
            counter++;
        }

        // converting char array to base 10
        char *ptr;
        uint32_t newTotalBytesReceived = strtol(totalBytesArray, &ptr, 10);
        ESP_LOGI(TAG, "tried to read number %i", newTotalBytesReceived);

        uint32_t bytesDelta = newTotalBytesReceived - totalBytesReceived;
        ESP_LOGI(TAG, "bytesDelta %i", bytesDelta);

        totalBytesReceived = newTotalBytesReceived;

        TickType_t currentCount = xTaskGetTickCount();
        float timeDelta = (currentCount - lastInvocation) / configTICK_RATE_HZ;
        ESP_LOGI(TAG, "timeDelta %f with rate %i Hz", timeDelta, configTICK_RATE_HZ);


        bandwidth = bytesDelta / timeDelta;
        ESP_LOGI(TAG, "calculated bandwidth %i", bandwidth);


        lastInvocation = currentCount;

        vTaskDelay(2500 / portTICK_RATE_MS);
    }

}

void setMeter() {

    ESP_ERROR_CHECK(dac_output_enable(DAC_CHANNEL_1));

    uint8_t verhaeltnis = MAX_INTERNET_BANDWIDTH / 255;
    uint8_t targetVoltage = 0;
    uint8_t intermediateVoltage = 0;
    while (true) {
        targetVoltage = bandwidth / verhaeltnis;

        if (intermediateVoltage < targetVoltage) {
            intermediateVoltage++;
        } else if (intermediateVoltage > targetVoltage) {
            intermediateVoltage--;
        }

        if (intermediateVoltage != targetVoltage) {
            ESP_LOGI(TAG, " %i Byte/s -> %i Vi -> %i Vi", bandwidth, intermediateVoltage, targetVoltage);
            ESP_ERROR_CHECK(dac_output_voltage(DAC_CHANNEL_1, intermediateVoltage));
            vTaskDelay(33 / portTICK_RATE_MS);
        }
    }
}

void app_main(void) {

    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(getValue, "getValue", 4096, NULL, 5, NULL);
    xTaskCreate(setMeter, "setMeter", 4096, NULL, 5, NULL);

}
