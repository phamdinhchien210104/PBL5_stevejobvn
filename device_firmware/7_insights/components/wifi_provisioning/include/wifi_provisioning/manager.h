#pragma once

#include "network_provisioning/manager.h"

#define wifi_prov_mgr_config_t                      network_prov_mgr_config_t
#define wifi_prov_mgr_init                          network_prov_mgr_init
#define wifi_prov_mgr_is_provisioned                network_prov_mgr_is_wifi_provisioned
#define wifi_prov_security_t                        network_prov_security_t
#define wifi_prov_mgr_endpoint_create               network_prov_mgr_endpoint_create
#define wifi_prov_mgr_start_provisioning            network_prov_mgr_start_provisioning
#define wifi_prov_mgr_endpoint_register             network_prov_mgr_endpoint_register
#define wifi_prov_mgr_deinit                        network_prov_mgr_deinit
#define wifi_prov_sta_fail_reason_t                 network_prov_wifi_sta_fail_reason_t
#define wifi_prov_mgr_reset_sm_state_on_failure     network_prov_mgr_reset_wifi_sm_state_on_failure

#define WIFI_PROV_EVENT                             NETWORK_PROV_EVENT
#define WIFI_PROV_START                             NETWORK_PROV_START
#define WIFI_PROV_CRED_RECV                         NETWORK_PROV_WIFI_CRED_RECV
#define WIFI_PROV_CRED_FAIL                         NETWORK_PROV_WIFI_CRED_FAIL
#define WIFI_PROV_CRED_SUCCESS                      NETWORK_PROV_WIFI_CRED_SUCCESS
#define WIFI_PROV_END                               NETWORK_PROV_END
#define WIFI_PROV_STA_AUTH_ERROR                    NETWORK_PROV_WIFI_STA_AUTH_ERROR
#define WIFI_PROV_STA_AP_NOT_FOUND                  NETWORK_PROV_WIFI_STA_AP_NOT_FOUND

#define WIFI_PROV_SECURITY_0                        NETWORK_PROV_SECURITY_0
#define WIFI_PROV_SECURITY_1                        NETWORK_PROV_SECURITY_1
#define WIFI_PROV_SECURITY_2                        NETWORK_PROV_SECURITY_2

#define WIFI_PROV_EVENT_HANDLER_NONE                NETWORK_PROV_EVENT_HANDLER_NONE
