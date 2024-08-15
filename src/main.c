#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <usbdrvce.h>
#include <ti/getcsc.h>
#include <ti/screen.h>
#include <ti/vars.h>
#include <sys/timers.h>

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"

#include "lwip/altcp_tcp.h"
#include "lwip/altcp.h"
#include "lwip/dhcp.h"

#include "drivers/usb-ethernet.h"

#include "netchat-lib/netchat.h"
#include "http-client/http-client.h"

#define MAX_INPUT_LENGTH 64
#define RANDOM_STRING_LENGTH 256

enum
{
    INPUT_LOWER,
    INPUT_UPPER,
    INPUT_NUMBER
};
char *chars_lower = "\0\0\0\0\0\0\0\0\0\0\"wrmh\0\0?[vqlg\0\0.zupkfc\0 ytojeb\0\0xsnida\0\0\0\0\0\0\0\0";
char *chars_upper = "\0\0\0\0\0\0\0\0\0\0\"WRMH\0\0?[VQLG\0\0:ZUPKFC\0 YTOJEB\0\0XSNIDA\0\0\0\0\0\0\0\0";
char *chars_num = "\0\0\0\0\0\0\0\0\0\0+-*/^\0\0?359)\0\0\0.258(\0\0\0000147,\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char mode_indic[] = {'a', 'A', '1'};
bool outchar_scroll_up = true;

struct netif *ethif = NULL;
err_t tcp_connect_callback(void *arg, struct altcp_pcb *tpcb, err_t err);

bool run_main = false;
bool lock_due_to_http = false;
// Comment to trigger action
// This is the official NETCHAT server located at netchat.tkbstudios.com
// If you want to use your own server, set it here.
// Make sure to use an IP ADDRESS in ASCII format!
// For ex. 255.255.255.255
ChatServer default_server = {
    .host = "152.228.162.35",
    .port = 2052,
    .online_mode = false,
    .server_password = ""
};

static void newline(void)
{
    if (outchar_scroll_up)
    {
        memmove(gfx_vram, gfx_vram + (LCD_WIDTH * 10), LCD_WIDTH * (LCD_HEIGHT - 30));
        gfx_SetColor(255);
        gfx_FillRectangle_NoClip(0, LCD_HEIGHT - 30, LCD_WIDTH, 10);
        gfx_SetTextXY(2, LCD_HEIGHT - 30);
    }
    else
        gfx_SetTextXY(2, gfx_GetTextY() + 10);
}
void outchar(char c)
{
    if (c == '\n')
    {
        newline();
    }
    else if (c < ' ' || c > '~')
    {
        return;
    }
    else
    {
        if (gfx_GetTextX() >= LCD_WIDTH - 16)
        {
            newline();
        }
        gfx_PrintChar(c);
    }
}

void reset_cursor_position() {
    gfx_SetTextXY(2, gfx_GetTextY() + 10);
}

void clear_input_buffer() {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF);
}

void ethif_status_callback_fn(struct netif *netif)
{
    return;
    // if (netif->flags & NETIF_FLAG_LINK_UP)
    // {
    //     printf("Link up\n");
    // }
    // else
    // {
    //     printf("Link down\n");
    // }
}

void exit_funcs(void)
{
    netchat_destroy();
    usb_Cleanup();
    gfx_End();
}

void message_received_callback(IncomingMessage msg)
{
    newline();
    printf("[%lu] %s: %s\n", msg.timestamp, msg.sender, msg.message);
    return;
}

void handle_all_events()
{
    usb_HandleEvents();       // usb events
    sys_check_timeouts();     // lwIP timers/event callbacks
}

void display_mode_indicators()
{
    gfx_SetTextFGColor(11);
    gfx_PrintChar(mode_indic[INPUT_LOWER]);
    gfx_SetTextFGColor(0);
    gfx_SetTextXY(2, LCD_HEIGHT - 30);
}
void display_input_box(const char *text, const char *placeholder, bool show_placeholder)
{
    gfx_SetColor(255);
    gfx_FillRectangle(0, 220, 320, 20);
    gfx_SetColor(0);
    gfx_HorizLine(0, 220, 320);
    gfx_SetTextXY(0, 221);
    if (show_placeholder)
    {
        gfx_SetTextFGColor(128);
        printf("%s", placeholder);
        gfx_SetTextFGColor(0);
    }
    else
    {
        printf("%s", text);
    }
}

void get_text_input(char *placeholder, char *input, size_t max_length)
{
    sk_key_t key;
    char *ref_str = chars_lower;
    uint8_t input_mode = INPUT_LOWER;
    uint8_t string_length = 0;
    bool string_changed = true;
    bool show_placeholder = true;

    display_input_box("", placeholder, true);

    while (1)
    {
        key = os_GetCSC();
        if (key == sk_Clear)
        {
            input[0] = '\0';
            return;
        }
        if (key == sk_Enter)
        {
            if (string_length > 0)
            {
                input[string_length] = '\0';
                clear_input_buffer();
                return;
            }
        }
        else if (key == sk_Mode || key == sk_Alpha)
        {
            input_mode++;
            if (input_mode > INPUT_NUMBER)
                input_mode = INPUT_LOWER;
            ref_str = (input_mode == INPUT_LOWER) ? chars_lower : (input_mode == INPUT_UPPER) ? chars_upper : chars_num;
            string_changed = true;
        }
        else if (key == sk_Del)
        {
            if (string_length > 0)
            {
                input[--string_length] = 0;
                string_changed = true;
            }
        }
        else if (ref_str[key] && (string_length < max_length - 1))
        {
            if (show_placeholder)
            {
                show_placeholder = false;
                string_length = 0;
            }
            input[string_length++] = ref_str[key];
            string_changed = true;
        }
        if (string_changed)
        {
            display_input_box(input, placeholder, show_placeholder);
            gfx_SetTextFGColor(11);
            gfx_PrintChar(mode_indic[input_mode]);
            gfx_SetTextFGColor(0);
            gfx_SetTextXY(2, LCD_HEIGHT - 30);
            string_changed = false;
        }
        handle_all_events();
    }
}

void generate_random_string(char *str, size_t length)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < length; i++)
    {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[length] = '\0';
}

static void http_get_recv(struct pbuf *p, err_t err) {
    printf("HTTP rcv:\n%.*s", p->len, (char *)p->payload);
}

int main(void)
{
    sk_key_t key = 0;
    char username[MAX_INPUT_LENGTH] = {0};
    char random_string[RANDOM_STRING_LENGTH + 1] = {0};
    char chat_string[MAX_INPUT_LENGTH] = {0};

    srand(time(NULL));

    os_ClrHome();
    gfx_Begin();
    newline();
    gfx_SetTextXY(2, LCD_HEIGHT - 30);

    get_text_input("Enter your username: ", username, MAX_INPUT_LENGTH);
    printf("Welcome, %s\n", username);

    lwip_init();

    if (usb_Init(eth_handle_usb_event, NULL, NULL, USB_DEFAULT_INIT_FLAGS))
        goto exit;

    run_main = true;

    printf("Waiting for interface...\n");
    while (ethif == NULL)
    {
        key = os_GetCSC();
        if (key == sk_Clear) {
            goto exit;
        }
        ethif = netif_find("en0");
        handle_all_events();
    }
    printf("interface found\n");
    printf("DHCP starting...\n");
    netif_set_status_callback(ethif, ethif_status_callback_fn);
    if (dhcp_start(ethif) != ERR_OK)
    {
        printf("dhcp_start failed\n");
        goto exit;
    }
    printf("dhcp started\n");

    printf("waiting for DHCP to complete...\n");
    while (!dhcp_supplied_address(ethif))
    {
        key = os_GetCSC();
        if (key == sk_Clear) {
            goto exit;
        }
        handle_all_events();
    }
    printf("DHCP completed\n");

    printf("Testing HTTP GET...\n");
    if (http_request(HTTP_GET, default_server.host, 80, "/", "", "", 1024, http_get_recv) != ERR_OK)
    {
        printf("HTTP GET failed\n");
        msleep(1000);
        goto exit;
    }
    printf("Waiting for HTTP response...\n");
    while (1)
    {
        key = os_GetCSC();
        if (key == sk_Clear) {
            break;
        }
        handle_all_events();
    }

    printf("Connecting to %s:%d\n", default_server.host, default_server.port);
    netchat_init(ethif, default_server, message_received_callback);
    while (!netchat_is_connected()) {
        key = os_GetCSC();
        if (key == sk_Clear) {
            goto exit;
        }
        handle_all_events();
    }
    printf("Connection established!\n");
    printf("Does server need valid creds?: %s\n", default_server.online_mode ? "yes" : "no");
    msleep(200);
    generate_random_string(random_string, RANDOM_STRING_LENGTH);
    if (netchat_login(username, random_string) == NETCHAT_OK)
    {
        printf("Login request success\n");
        printf("Waiting for login response...\n");
        while (!netchat_is_logged_in())
        {
            key = os_GetCSC();
            if (key == sk_Clear) {
                goto exit;
            }
            handle_all_events();
        }
        printf("Login success\n");
    }
    else
    {
        printf("Login request failed\n");
        goto exit;
    }
    
    run_main = true;
    display_input_box("", "Press enter to send a message", true);
    
    while (run_main)
    {
        if (!netchat_is_connected())
        {
            printf("Connection lost\n");
            run_main = false;
        }
        if (!netchat_is_logged_in())
        {
            printf("Not logged in\n");
            run_main = false;
        }

        key = os_GetCSC();
        handle_all_events();

        if (key == sk_Clear)
        {
            run_main = false;
        }
        else if (key == sk_Enter)
        {
            get_text_input("Message: ", chat_string, MAX_INPUT_LENGTH);
            if (strlen(chat_string) > 0)
            {
                OutgoingMessage msg;
                msg.recipient = "global";
                msg.message = chat_string;
                if (netchat_send(&msg) != NETCHAT_OK)
                {
                    printf("Failed to send message\n");
                }
                display_input_box("", "Press enter to send a message", true);
            }
        }
    }
    goto exit;
exit:
    exit_funcs();
    exit(0);
}
