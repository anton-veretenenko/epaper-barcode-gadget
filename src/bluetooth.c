// based on https://github.com/Zeni241/ESP32-NimbleBLE-For-Dummies

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bluetooth.h"
#include "file_roller.h"
#include "display.h"

#define EB_MAX_TRANSFER_SIZE 512
enum {
    EB_OP_NONE,
    EB_OP_WRITE,
    EB_OP_DELETE
};

//@______________________Declare some variables____________________________
static const char *device_name = "eBarcoder";
esp_err_t ret;
static const char *tag = "NimBLE_BLE";
static uint8_t mac_address[6] = {0};
static uint8_t own_addr_type;
static uint16_t conn_handle;
static uint16_t char_value_handle_filename;
static uint16_t char_value_handle_operation;
static uint16_t char_value_handle_data;
static FILE *write_file_handle = NULL;
//@_____________________Define UUIDs______________________________________
// f5fe6e39-a786-44ae-bbdc-232ca38f0000 // service uuid
// characteristics
// f5fe6e39-a786-44ae-bbdc-232ca38f0001 // set/read current filename
// f5fe6e39-a786-44ae-bbdc-232ca38f0002 // set operation
// f5fe6e39-a786-44ae-bbdc-232ca38f0003 // set data
static const ble_uuid128_t gatt_svr_svc_uuid = // service uuid
    BLE_UUID128_INIT(0x00, 0x00, 0x8f, 0xa3, 0x2c, 0x23, 0xdc, 0xbb, 0xae, 0x44, 0x86, 0xa7, 0x39, 0x6e, 0xfe, 0xf5);
static const ble_uuid128_t gatt_svr_chr_uuid_filename =
    BLE_UUID128_INIT(0x01, 0x00, 0x8f, 0xa3, 0x2c, 0x23, 0xdc, 0xbb, 0xae, 0x44, 0x86, 0xa7, 0x39, 0x6e, 0xfe, 0xf5);
static const ble_uuid128_t gatt_svr_chr_uuid_operation =
    BLE_UUID128_INIT(0x02, 0x00, 0x8f, 0xa3, 0x2c, 0x23, 0xdc, 0xbb, 0xae, 0x44, 0x86, 0xa7, 0x39, 0x6e, 0xfe, 0xf5);
    static const ble_uuid128_t gatt_svr_chr_uuid_data =
    BLE_UUID128_INIT(0x03, 0x00, 0x8f, 0xa3, 0x2c, 0x23, 0xdc, 0xbb, 0xae, 0x44, 0x86, 0xa7, 0x39, 0x6e, 0xfe, 0xf5);
static const ble_uuid16_t gatt_svr_svc_uuid_marker = BLE_UUID16_INIT(0xB8CD); // marker uuid in advertise packet to detect device

//@_____Some variables used in service and characteristic declaration______
static char char_filename_buf[FL_MAX_NAME] = "filename";
static char char_data_buf[EB_MAX_TRANSFER_SIZE];
static uint8_t char_operation = EB_OP_NONE;

//@_____________Forward declaration of some functions ___________
static int gatt_svr_init(void);
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

static int char_access_filename(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);
static int char_access_operation(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);
static int char_access_data(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);

static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len); //!! Callback function. When ever user write to this characterstic,this function will execute

static void bleprph_on_reset(int reason);
void bleprph_host_task(void *param);
static void bleprph_on_sync(void);
void print_bytes(const uint8_t *bytes, int len);
void print_addr(const void *addr);
//@___________________________Heart of nimble code _________________________________________

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {

        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid_filename.u,
                .access_cb = char_access_filename, // callback
                .val_handle = &char_value_handle_filename,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE, // read/write
            },
            {
                .uuid = &gatt_svr_chr_uuid_operation.u,
                .access_cb = char_access_operation,
                .val_handle = &char_value_handle_operation,
                .flags = BLE_GATT_CHR_F_WRITE, // write only
            },
            {
                .uuid = &gatt_svr_chr_uuid_data.u,
                .access_cb = char_access_data,
                .val_handle = &char_value_handle_data,
                .flags = BLE_GATT_CHR_F_WRITE, // write only
            },
            {0,} // end of characteristics, necessary
        },
    },

    {
        0, /* No more services. This is necessary */
    },
};

static int char_access_filename(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    int rc;
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: //!! In case user accessed this characterstic to read its value, bellow lines will execute
            rc = os_mbuf_append(ctxt->om, &char_filename_buf,
                                strlen(char_filename_buf));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

        case BLE_GATT_ACCESS_OP_WRITE_CHR: //!! In case user accessed this characterstic to write, bellow lines will executed.
            rc = gatt_svr_chr_write(ctxt->om, 1, sizeof(char_filename_buf)-1, &char_filename_buf, NULL); //!! Function "gatt_svr_chr_write" will fire.
            // terminate the string
            if (ctxt->om->om_len > sizeof(char_filename_buf)) {
                char_filename_buf[sizeof(char_filename_buf)-1] = '\0';
            } else {
                char_filename_buf[ctxt->om->om_len] = '\0';
            }
            ESP_LOGI(tag, "rc=%d Received=%s\n", rc, char_filename_buf); // Print the received value
            // reset operation
            char_operation = EB_OP_NONE;
            return rc;
        default:
            assert(0);
            ESP_LOGE(tag, "Unknown op=%d\n", ctxt->op);
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int char_access_operation(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    int rc;
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            rc = gatt_svr_chr_write(ctxt->om, 1, 1, &char_operation, NULL);
            ESP_LOGI(tag, "rc=%d Operation=%d\n", rc, char_operation);
            switch (char_operation) {
                case EB_OP_WRITE: {
                    if (strlen(char_filename_buf) > 0 && \
                        strlen(char_filename_buf) < FL_MAX_NAME && \
                        fl_space_available(DISPLAY_WIDTH*DISPLAY_HEIGHT/8)) {
                        ESP_LOGI(tag, "Write operation requested for file %s\n", char_filename_buf);
                        write_file_handle = fl_init_write(char_filename_buf);
                    } else {
                        ESP_LOGE(tag, "filename length error or free space not available");
                        char_operation = EB_OP_NONE;
                    }
                } break;
                
                case EB_OP_DELETE: {
                    if (strlen(char_filename_buf) > 0 && \
                        strlen(char_filename_buf) < FL_MAX_NAME && \
                        fl_exists(char_filename_buf)) {
                        ESP_LOGI(tag, "Delete operation requested %s\n", char_filename_buf);
                    } else {
                        ESP_LOGI(tag, "Delete operation requested %s\n", char_filename_buf);
                        ESP_LOGE(tag, "filename length error or file not exists");
                        char_operation = EB_OP_NONE;
                    }
                } break;

                default: break;
            }
            return rc;
        default:
            assert(0);
            ESP_LOGE(tag, "Unknown op=%d\n", ctxt->op);
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int char_access_data(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    int rc;
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            uint16_t written;
            rc = gatt_svr_chr_write(ctxt->om, 1, sizeof(char_data_buf), &char_data_buf, &written);
            ESP_LOGI(tag, "rc=%d Data bytes written=%d\n", rc, written);

            switch (char_operation) {
                case EB_OP_DELETE: {
                    ESP_LOGI(tag, "Deleting file %s\n", char_filename_buf);
                    fl_delete(char_filename_buf);
                    char_operation = EB_OP_NONE;
                } break;
                
                case EB_OP_WRITE: {
                    ESP_LOGI(tag, "Writing request to file %s\n", char_filename_buf);
                    if (write_file_handle) {
                        fwrite(char_data_buf, 1, written, write_file_handle);
                        ESP_LOGI(tag, "Written %d bytes\n", written);
                        int bytes = ftell(write_file_handle);
                        ESP_LOGI(tag, "File size: %d\n", bytes);
                        if (bytes >= DISPLAY_WIDTH*DISPLAY_HEIGHT/8) {
                            fclose(write_file_handle);
                            write_file_handle = NULL;
                            ESP_LOGI(tag, "File closed\n");
                            char_operation = EB_OP_NONE;
                            fl_deinit();
                            fl_init(BARCODES_PATH);
                        }
                    } else {
                        ESP_LOGE(tag, "File not open, resetting operation to None");
                        char_operation = EB_OP_NONE;
                    }
                } break;

                default: break;
            }
            return rc;
        default:
            assert(0);
            ESP_LOGE(tag, "Unknown op=%d\n", ctxt->op);
            return BLE_ATT_ERR_UNLIKELY;
    }
}

void bluetooth_start() //! Call this function to start BLE
{
    //! Below is the sequence of APIs to be called to init/enable NimBLE host and ESP controller:
    printf("\n Staring BLE \n");
    int rc;

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nimble_port_init());
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_sc = 0;

    rc = gatt_svr_init();
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set(device_name); //!! Set the name of this device
    assert(rc == 0);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);

    /* XXX Need to have template for store */

    nimble_port_freertos_init(bleprph_host_task);
    printf("char_filename_buf at end of startBLE=%s\n", char_filename_buf);
}

void bluetooth_stop() //! Call this function to stop BLE
{
    //! Below is the sequence of APIs to be called to disable/deinit NimBLE host and ESP controller:
    printf("\n Stoping BLE and notification task \n");
    // vTaskDelete(xHandle);
    int ret = nimble_port_stop();
    if (ret == 0)
    {
        nimble_port_deinit();
    }
}
//@________________Bellow code will remain as it is.Take it abracadabra of BLE 😀 😀________________

static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len)
    {
        ESP_LOGI(tag, "om_len=%d min_len=%d max_len=%d\n", om_len, min_len, max_len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0)
    {
        ESP_LOGE(tag, "ble_hs_mbuf_to_flat failed; rc=%d\n", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

static int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}

static void
bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                      "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

static void
bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    // fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl = -12 + esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV)*3;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    // fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(0xB8CD) };
    fields.uuids16 = &gatt_svr_svc_uuid_marker;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 0;
    // fields.uuids128 = &gatt_svr_svc_uuid;
    // fields.num_uuids128 = 1;
    // fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x0200;
    adv_params.itvl_max = 0x0200;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            // bleprph_print_conn_desc(&desc);
        }
        MODLOG_DFLT(INFO, "\n");

        if (event->connect.status != 0)
        {
            /* Connection failed; resume advertising. */
            bleprph_advertise();
        }
        conn_handle = event->connect.conn_handle;
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        // bleprph_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");

        if (write_file_handle)
        {
            fclose(write_file_handle);
            write_file_handle = NULL;
        }
        char_operation = EB_OP_NONE;

        /* Connection terminated; resume advertising. */
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        // bleprph_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;
    }

    return 0;
}

static void
bleprph_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
bleprph_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    rc = ble_hs_id_copy_addr(own_addr_type, mac_address, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(mac_address);
    MODLOG_DFLT(INFO, "\n");
    /* Begin advertising. */
    bleprph_advertise();
}

void bluetooth_get_mac(uint8_t mac[6])
{
    memcpy(mac, mac_address, 6);
}

void bleprph_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

//@____________________________________________________________________

/**
 * Utility function to log an array of bytes.
 */
void
print_bytes(const uint8_t *bytes, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        MODLOG_DFLT(INFO, "%s0x%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

void
print_addr(const void *addr)
{
    const uint8_t *u8p;

    u8p = addr;
    MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x",
                u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}