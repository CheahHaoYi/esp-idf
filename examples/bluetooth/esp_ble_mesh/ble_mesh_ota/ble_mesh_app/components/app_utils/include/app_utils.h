// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __APP_UTILS_H__
#define __APP_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

// #define FUNC_TRACE 1

#ifdef FUNC_TRACE
#define ENTER_FUNC() ESP_LOGI(TAG, "enter: %s, %d", __FUNCTION__, __LINE__)
#define EXIT_FUNC()  ESP_LOGI(TAG, "exit: %s, %d", __FUNCTION__, __LINE__)
#else
#define ENTER_FUNC()
#define EXIT_FUNC()
#endif

/**
 * Macro which can be used to check the error code,
 * and terminate the program in case the code is not ESP_OK.
 * Prints the error code, error location, and the failed statement to serial output.
 *
 * Disabled if assertions are disabled.
 */
#define UTILS_ERROR_CHECK(con, err, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "<%s> " format, esp_err_to_name(err), ##__VA_ARGS__); \
            return err; \
        } \
    } while(0)

/**
 * @brief restart count erase timer callback function.
 *
 * @return
 *     - NULL
 */
void restart_count_erase_timercb(void *timer);

/**
 * @brief Get restart count.
 *
 * @return
 *     - count
 */
int restart_count_get(void);

/**
 * @brief Determine if the restart is caused by an exception.
 *
 * @return
 *     - true
 *     - false
 */
bool restart_is_exception(void);

/**
 * @brief Periodically print system information.
 *
 * @param timer pointer of timer
 *
 * @return
 *     - NULL
 */
__attribute__((weak)) void show_system_info_timercb(void *timer);

/**
 * @brief
 *
 * @return esp_err_t
 */
esp_err_t upgrade_version_rollback();

/**
 * @brief
 *
 * @return esp_err_t
 */
esp_err_t print_partition_table();

#ifdef __cplusplus
}
#endif

#endif /**< __APP_UTILS_H__ */
