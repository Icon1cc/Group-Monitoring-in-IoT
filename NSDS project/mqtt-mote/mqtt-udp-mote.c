#include "contiki.h"
#include "random.h"
#include "string.h"
#include "lib/list.h"
#include "lib/memb.h"

#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"

#include "uip-ds6-nbr.h"
#include "nbr-table.h"

#include "mqtt.h"
#include "rpl.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/sicslowpan.h"
#include "sys/etimer.h"
#include "sys/ctimer.h"

#include "sys/log.h"
#define LOG_MODULE "Client"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_PORT 5555
#define COMMUNICATION_LAG (CLOCK_SECOND >> 1)
#define SEND_INTERVAL (CLOCK_SECOND * 20)
static const uip_ipaddr_t *my_ipaddr;

#define MQTT_BROKER_IP_ADDR "fd00::1"
#define MQTT_PUB_TOPIC_CONTACTS "nsds_gm/contacts/"
static const char *broker_ip = MQTT_BROKER_IP_ADDR;

#define DEFAULT_ORG_ID "mqtt-client"
#define DEFAULT_TYPE_ID "native"
#define DEFAULT_AUTH_TOKEN "AUTHZ"
#define DEFAULT_SUBSCRIBE_CMD_TYPE "+"
#define DEFAULT_BROKER_PORT 1883
#define DEFAULT_PUBLISH_INTERVAL (CLOCK_SECOND * 5)
#define DEFAULT_KEEP_ALIVE_TIMER 10

#define NET_CONNECT_PERIODIC (CLOCK_SECOND >> 2)
#define NET_RETRY (CLOCK_SECOND * 10)

#define MAX_TCP_SEGMENT_SIZE 32
#define ADDRESS_SIZE 32
#define BUFFER_SIZE 128
#define APP_BUFFER_SIZE 256
#define MAX_CONTACTS 128
#define CONTACT_TIMEOUT (CLOCK_SECOND * 30)

#define STATE_MACHINE_PERIODIC (CLOCK_SECOND * 1)
#define RECONNECT_INTERVAL (CLOCK_SECOND * 2)
#define CONNECTION_STABLE_TIME (CLOCK_SECOND * 5)
static struct timer connection_life;
static uint8_t connect_attempt;
static uint8_t state;
#define STATE_INIT 0
#define STATE_REGISTERED 1
#define STATE_CONNECTING 2
#define STATE_CONNECTED 3
#define STATE_SUBSCRIBED 4
#define STATE_DISCONNECTED 5
#define STATE_ERROR 0xFF

// Client configuration values
#define CONFIG_ORG_ID_LEN 32
#define CONFIG_TYPE_ID_LEN 32
#define CONFIG_AUTH_TOKEN_LEN 32
#define CONFIG_CMD_TYPE_LEN 8
#define CONFIG_IP_ADDR_STR_LEN 64
#define DEFAULT_TYPE_ID "native"
#define DEFAULT_AUTH_TOKEN "AUTHZ"
#define DEFAULT_SUBSCRIBE_CMD_TYPE "+"
#define DEFAULT_BROKER_PORT 1883
#define DEFAULT_PUBLISH_INTERVAL (CLOCK_SECOND * 5)
#define DEFAULT_KEEP_ALIVE_TIMER 10
#define NET_CONNECT_PERIODIC (CLOCK_SECOND >> 2)
#define NET_RETRY (CLOCK_SECOND * 10)
#define MAX_TCP_SEGMENT_SIZE 32
#define ADDRESS_SIZE 32
#define BUFFER_SIZE 128
#define APP_BUFFER_SIZE 256
#define CONTACT_INACTIVITY_THRESHOLD (CLOCK_SECOND * 60)

// Process declarations
PROCESS(udp_client_process, "UDP client");
PROCESS(mqtt_client_process, "MQTT");
AUTOSTART_PROCESSES(&udp_client_process, &mqtt_client_process);

// Function declarations
static void report_group_formation(void);
static struct simple_udp_connection udp_conn;

static char addr_buffer[ADDRESS_SIZE];
static char *trim_ip_addr(const uip_ipaddr_t *ip_addr)
{
    char *addr_ptr = addr_buffer;
    uiplib_ipaddr_snprint(addr_ptr, ADDRESS_SIZE, ip_addr);
    addr_ptr += 6;
    return addr_ptr;
}

typedef struct mqtt_client_config
{
    char org_id[CONFIG_ORG_ID_LEN];
    char type_id[CONFIG_TYPE_ID_LEN];
    char auth_token[CONFIG_AUTH_TOKEN_LEN];
    char broker_ip[CONFIG_IP_ADDR_STR_LEN];
    char cmd_type[CONFIG_CMD_TYPE_LEN];
    clock_time_t pub_interval;
    uint16_t broker_port;
} mqtt_client_config_t;

static struct mqtt_connection conn;
static mqtt_client_config_t conf;

static char app_buffer[APP_BUFFER_SIZE];
static char client_id[BUFFER_SIZE];
static char pub_topic_contacts[BUFFER_SIZE];
static char pub_topic_signals[BUFFER_SIZE];
static char sub_topic[BUFFER_SIZE];
static char *buf_ptr;

static struct mqtt_message *msg_ptr = 0;
static struct etimer fsm_periodic_timer;

// Data structures for contact management
typedef struct mutual_contact
{
    struct mutual_contact *next;
    uip_ipaddr_t ipaddr;
} mutual_contact_t;

typedef struct contact
{
    struct contact *next;
    uip_ipaddr_t ipaddr;
    clock_time_t last_seen;
    clock_time_t last_activity;
    LIST_STRUCT(mutual_contacts);
} contact_t;

// Memory blocks for contacts and mutual contacts

MEMB(contacts_memb, contact_t, MAX_CONTACTS);
MEMB(mutual_contacts_mem, mutual_contact_t, MAX_CONTACTS *MAX_CONTACTS);
LIST(contacts_list);

bool should_be_mutual_contact(contact_t *contact1, contact_t *contact2)
{
    return (clock_time() - contact1->last_seen <= CONTACT_TIMEOUT) &&
           (clock_time() - contact2->last_seen <= CONTACT_TIMEOUT);
}

// Function to check if a contact is inactive and remove it from the list
static void check_contact_activity(void)
{
    contact_t *contact = list_head(contacts_list);
    contact_t *next_contact;

    while (contact != NULL)
    {
        next_contact = list_item_next(contact); // Store next contact in case current one is removed

        if (clock_time() - contact->last_activity > CONTACT_INACTIVITY_THRESHOLD)
        {
            LOG_INFO("Contact left: %s\n", trim_ip_addr(&contact->ipaddr));

            char departure_message[APP_BUFFER_SIZE];
            snprintf(departure_message, sizeof(departure_message),
                     "{\"event\": \"departure\", \"ip\": \"%s\"}", trim_ip_addr(&contact->ipaddr));
            mqtt_publish(&conn, NULL, pub_topic_contacts, (uint8_t *)departure_message,
                         strlen(departure_message), MQTT_QOS_LEVEL_1, MQTT_RETAIN_OFF);

            list_remove(contacts_list, contact);
            memb_free(&contacts_memb, contact);
        }

        contact = next_contact;
    }
}

// Function to add a mutual contact to a contact's list
void add_to_mutual_contacts_if_missing(contact_t *contact, const uip_ipaddr_t *addr)
{
    mutual_contact_t *mutual_contact = list_head(contact->mutual_contacts);
    while (mutual_contact != NULL)
    {
        if (uip_ipaddr_cmp(&mutual_contact->ipaddr, addr))
        {
            return;
        }
        mutual_contact = list_item_next(mutual_contact);
    }
    mutual_contact = memb_alloc(&mutual_contacts_mem);
    if (mutual_contact != NULL)
    {
        uip_ipaddr_copy(&mutual_contact->ipaddr, addr);
        list_add(contact->mutual_contacts, mutual_contact);
    }
}
static int count_potential_group_members(contact_t *c)
{
    if (c == NULL)
    {
        return 0;
    }
    return 1 + count_potential_group_members(list_item_next(c));
}

static contact_t *update_contact_recursive(contact_t *c, const uip_ipaddr_t *addr, bool *found)
{
    if (c == NULL)
    {
        return NULL;
    }

    if (uip_ipaddr_cmp(&c->ipaddr, addr))
    {
        c->last_seen = clock_time();
        LOG_INFO("Contact updated: %s\n", trim_ip_addr(addr));
        *found = true;
        return c;
    }

    return update_contact_recursive(list_item_next(c), addr, found);
}

// Function to recursively update a contact's last seen time
void update_mutual_contacts_recursive(contact_t *current_contact, contact_t *other_contacts)
{
    if (other_contacts == NULL)
        return;

    if (current_contact != other_contacts)
    {
        if (should_be_mutual_contact(current_contact, other_contacts))
        {
            add_to_mutual_contacts_if_missing(current_contact, &other_contacts->ipaddr);
            add_to_mutual_contacts_if_missing(other_contacts, &current_contact->ipaddr);
        }
    }
    update_mutual_contacts_recursive(current_contact, list_item_next(other_contacts));
}

// Function to recursively update mutual contacts
void update_all_mutual_contacts(contact_t *contacts)
{
    if (contacts == NULL)
        return;
    update_mutual_contacts_recursive(contacts, list_head(contacts_list));
    update_all_mutual_contacts(list_item_next(contacts));
}

// Function to recursively update mutual contacts
static void update_contact(const uip_ipaddr_t *addr)
{
    bool found = false;
    update_contact_recursive(list_head(contacts_list), addr, &found);
    contact_t *contact = list_head(contacts_list);

    while (contact != NULL)
    {
        if (uip_ipaddr_cmp(&contact->ipaddr, addr))
        {
            contact->last_seen = clock_time();
            contact->last_activity = clock_time();
            found = true;
            break;
        }
        contact = list_item_next(contact);
    }

    if (!found)
    {
        contact = memb_alloc(&contacts_memb);
        if (contact != NULL)
        {
            uip_ipaddr_copy(&contact->ipaddr, addr);
            contact->last_seen = clock_time();
            LIST_STRUCT_INIT(contact, mutual_contacts);
            list_add(contacts_list, contact);
        }
    }
    update_all_mutual_contacts(list_head(contacts_list));
}

// Function to recursively check if a group is formed
bool is_group_formed(contact_t *contact, int *member_count)
{
    if (contact == NULL)
    {
        return true;
    }

    int mutual_count = 0;
    mutual_contact_t *mutual_contact = list_head(contact->mutual_contacts);
    while (mutual_contact != NULL)
    {
        mutual_count++;
        mutual_contact = list_item_next(mutual_contact);
    }

    if (mutual_count < 2)
    {
        return false;
    }

    *member_count += 1;

    return is_group_formed(list_item_next(contact), member_count);
}

// Function to recursively count mutual contactsa
static void check_group_formation()
{
    int member_count = count_potential_group_members(list_head(contacts_list));
    if (member_count >= 3)
    {
        report_group_formation();
    }
}

// Function to recursively add IP addresses to a JSON array
static void add_ip_to_json_array(char *json_array, const char *ip, size_t max_len)
{
    size_t current_len = strlen(json_array);
    size_t ip_len = strlen(ip);
    if (current_len + ip_len + 3 < max_len)
    {
        if (current_len > 1)
        {
            strcat(json_array, ",");
        }
        strcat(json_array, "\"");
        strcat(json_array, ip);
        strcat(json_array, "\"");
    }
}

// Function to recursively add IP addresses to a JSON array
static void add_ip_to_json_array_recursive(contact_t *c, char *json_array, size_t max_len)
{
    if (c == NULL)
    {
        strcat(json_array, "]");
        return;
    }
    char ip_str[ADDRESS_SIZE];
    snprintf(ip_str, ADDRESS_SIZE, "%s", trim_ip_addr(&c->ipaddr));
    add_ip_to_json_array(json_array, ip_str, max_len);
    add_ip_to_json_array_recursive(list_item_next(c), json_array, max_len);
}

// Function to report group formation to the backend
void report_group_formation()
{
    int len;
    int remaining = APP_BUFFER_SIZE;
    static char pub_topic[BUFFER_SIZE];

    buf_ptr = app_buffer;

    LOG_INFO("Reporting group formation.\n");
    char json_array[APP_BUFFER_SIZE] = {'[', '\0'};
    add_ip_to_json_array_recursive(list_head(contacts_list), json_array, sizeof(json_array));
    //strcat(json_array, "]");

    char message[APP_BUFFER_SIZE];
    snprintf(message, sizeof(message), "{\"group\": true, \"members\": %s}", json_array);

    //char message[APP_BUFFER_SIZE];
    len = snprintf(buf_ptr, remaining, "{\"group\": true, \"members\": %s}", json_array);
    if(len < 0 || len >= remaining) {
        LOG_ERR("Buffer too short. Have %d, need %d + \\0\n", remaining, len);
        return;
    }
    remaining -= len;
    buf_ptr += len;

    strcpy(pub_topic, pub_topic_contacts);

    mqtt_status_t status = mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer, strlen(app_buffer), MQTT_QOS_LEVEL_1, MQTT_RETAIN_OFF);
    if (status == MQTT_STATUS_OK || status == MQTT_STATUS_OUT_QUEUE_FULL)
    {
        LOG_INFO("Group formation reported: %s\n", message);
    }
    else
    {
        LOG_ERR("Failed to report group formation: status %d\n", status);
    }
}

/*---------------------------------------------------------------------------*/
// MQTT functions
static int construct_pub_topics(void)
{
    int len;
    int remaining = BUFFER_SIZE;
    buf_ptr = pub_topic_contacts;

    len = snprintf(buf_ptr, remaining, MQTT_PUB_TOPIC_CONTACTS);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Pub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }
    remaining -= len;
    buf_ptr += len;

    char *addr_ptr = trim_ip_addr(my_ipaddr);
    len = snprintf(buf_ptr, remaining, "%s", addr_ptr);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Pub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }
    remaining = BUFFER_SIZE;
    buf_ptr = pub_topic_signals;

    len = snprintf(buf_ptr, remaining, MQTT_PUB_TOPIC_SIGNALS);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Pub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }
    remaining -= len;
    buf_ptr += len;

    len = snprintf(buf_ptr, remaining, "%s", addr_ptr);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Pub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }

    return 1;
}
static int construct_sub_topic(void)
{
    int len;
    int remaining = BUFFER_SIZE;
    buf_ptr = sub_topic;

    len = snprintf(buf_ptr, remaining, MQTT_SUB_TOPIC);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Sub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }
    remaining -= len;
    buf_ptr += len;

    char *addr_ptr = trim_ip_addr(my_ipaddr);
    len = snprintf(buf_ptr, remaining, "%s", addr_ptr);
    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_ERR("Sub topic: %d, buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }
    return 1;
}
// Creates che clientID for MQTT communication
static int construct_client_id(void)
{
    int len = snprintf(client_id, BUFFER_SIZE, "d:%s:%s:%02x%02x%02x%02x%02x%02x",
                       conf.org_id, conf.type_id,
                       linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                       linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
                       linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

    if (len < 0 || len >= BUFFER_SIZE)
    {
        LOG_INFO("Client ID: %d, Buffer %d\n", len, BUFFER_SIZE);
        return 0;
    }

    return 1;
}
static void init_config()
{
    memset(&conf, 0, sizeof(mqtt_client_config_t));

    memcpy(conf.org_id, DEFAULT_ORG_ID, strlen(DEFAULT_ORG_ID));
    memcpy(conf.type_id, DEFAULT_TYPE_ID, strlen(DEFAULT_TYPE_ID));
    memcpy(conf.auth_token, DEFAULT_AUTH_TOKEN, strlen(DEFAULT_AUTH_TOKEN));
    memcpy(conf.broker_ip, broker_ip, strlen(broker_ip));
    memcpy(conf.cmd_type, DEFAULT_SUBSCRIBE_CMD_TYPE, 1);

    conf.broker_port = DEFAULT_BROKER_PORT;
    conf.pub_interval = DEFAULT_PUBLISH_INTERVAL;

    if (construct_client_id() == 0)
    {
        state = STATE_ERROR;
        return;
    }

    state = STATE_INIT;

    etimer_set(&fsm_periodic_timer, 0);
}
static void update_config(void)
{
    my_ipaddr = rpl_get_global_address();

    if (construct_sub_topic() == 0)
    {
        state = STATE_ERROR;
        return;
    }

    if (construct_pub_topics() == 0)
    {
        state = STATE_ERROR;
        return;
    }
    return;
}
static void connect_to_broker(void)
{
    // Connect to MQTT server
    mqtt_connect(&conn, conf.broker_ip, conf.broker_port, conf.pub_interval * 3);
    state = STATE_CONNECTING;
}
static void subscribe(void)
{
    mqtt_status_t status;

    status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);

    LOG_INFO("Subscribing\n");
    if (status == MQTT_STATUS_OUT_QUEUE_FULL)
    {
        LOG_INFO("Tried to subscribe but command queue was full!\n");
    }
}
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data)
{
    switch (event)
    {
    case MQTT_EVENT_CONNECTED:
    {
        LOG_INFO("Connected to the MQTT broker!\n");
        timer_set(&connection_life, CONNECTION_STABLE_TIME);
        state = STATE_CONNECTED;
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
    {
        LOG_INFO("Disconnected from the MQTT broker: reason %u\n", *((mqtt_event_t *)data));

        state = STATE_DISCONNECTED;
        process_poll(&mqtt_client_process);
        break;
    }
    case MQTT_EVENT_PUBLISH:
    {
        msg_ptr = data;
        if (msg_ptr->first_chunk)
        {
            LOG_INFO("NOTIFICATION received from mote with ID #%i \n", msg_ptr->payload_length);
        }
        break;
    }
    case MQTT_EVENT_SUBACK:
    {
        state = STATE_SUBSCRIBED;
        LOG_INFO("Application is subscribed to topic successfully\n");
        break;
    }
    case MQTT_EVENT_UNSUBACK:
    {
        LOG_INFO("Application is unsubscribed to topic successfully\n");
        break;
    }
    case MQTT_EVENT_PUBACK:
    {
        LOG_INFO("Publishing complete\n");
        break;
    }
    default:
        LOG_WARN("Application got a unhandled MQTT event: %i\n", event);
        break;
    }
}
static void state_machine(void)
{
    switch (state)
    {
    case STATE_INIT:
        LOG_INFO("STATE INIT\n");
        mqtt_register(&conn, &mqtt_client_process, client_id, mqtt_event, MAX_TCP_SEGMENT_SIZE);
        mqtt_set_username_password(&conn, "use-token-auth", conf.auth_token);

        conn.auto_reconnect = 0;
        connect_attempt = 1;

        state = STATE_REGISTERED;
    case STATE_REGISTERED:
        if (uip_ds6_get_global(ADDR_PREFERRED) != NULL)
        {
            update_config();
            LOG_INFO("Joined network! Connect attempt %u\n", connect_attempt);
            connect_to_broker();
        }
        etimer_set(&fsm_periodic_timer, NET_CONNECT_PERIODIC);
        return;
        break;
    case STATE_CONNECTING:
        LOG_INFO("Connecting: retry %u...\n", connect_attempt);
        break;
    case STATE_CONNECTED:
    case STATE_SUBSCRIBED:
        if (timer_expired(&connection_life))
        {
            connect_attempt = 0;
        }
        if (mqtt_ready(&conn) && conn.out_buffer_sent && state == STATE_CONNECTED)
        {
            subscribe();
        }
        break;
    case STATE_DISCONNECTED:
        mqtt_disconnect(&conn);
        connect_attempt++;

        LOG_INFO("Disconnected: attempt %u in %lu ticks\n", connect_attempt, NET_RETRY);
        etimer_set(&fsm_periodic_timer, NET_RETRY);

        state = STATE_REGISTERED;
        return;
    case STATE_ERROR:
        LOG_ERR("ERROR. Bad configuration.\n");
        return;
    default:
        LOG_INFO("ERROR. Default case: State=0x%02x\n", state);
        return;
    }

    etimer_set(&fsm_periodic_timer, STATE_MACHINE_PERIODIC);
}

// UDP callback function
static void udp_rx_callback(struct simple_udp_connection *c,
                            const uip_ipaddr_t *sender_addr, uint16_t sender_port,
                            const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
                            const uint8_t *data, uint16_t datalen)
{
    LOG_INFO("UDP callback received from %s\n", trim_ip_addr(sender_addr));
    if (uip_ipaddr_cmp(&sender_addr, &receiver_addr))
        return;

    // Update or add the sender as a contact
    update_contact(sender_addr);

    // Check if a group can be formed with the updated contacts list
    check_group_formation();

    
}

/*---------------------------------------------------------------------------*
                            UDP CLIENT PROCESS
/----------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
    static struct etimer udp_find_timer;
    static struct etimer udp_lag_timer;
    static uip_ds6_nbr_t *nbr;
    static uip_ipaddr_t *send_to_ipaddr;

    PROCESS_BEGIN();

    // Initialize UDP connection
    simple_udp_register(&udp_conn, UDP_PORT, NULL, UDP_PORT, udp_rx_callback);

    // Get one of this node's global addresses.
    my_ipaddr = rpl_get_global_address();

    // Starting udp communication periodic timer
    etimer_set(&udp_find_timer, random_rand() % SEND_INTERVAL);
    while (1)
    {
        PROCESS_WAIT_EVENT();
        if (ev == PROCESS_EVENT_TIMER && data == &udp_find_timer)
        {
            // Point to the head of the neighbors list
            nbr = nbr_table_head(ds6_neighbors);
            etimer_set(&udp_lag_timer, COMMUNICATION_LAG);
        }
        else if (ev == PROCESS_EVENT_TIMER && data == &udp_lag_timer)
        {
            if (nbr != NULL)
            {
                send_to_ipaddr = &(nbr->ipaddr);
                uint8_t isSignal = 0;
                simple_udp_sendto(&udp_conn, &isSignal, sizeof(isSignal), send_to_ipaddr);

                // Move pointer down the table stack
                nbr = nbr_table_next(ds6_neighbors, nbr);
                etimer_reset(&udp_lag_timer);
            }
            else
            {
                etimer_set(&udp_find_timer, SEND_INTERVAL - (5 * CLOCK_SECOND) + (random_rand() % (10 * CLOCK_SECOND)));
            }
        }
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*
                                MQTT PROCESS
/----------------------------------------------------------------------------*/
PROCESS_THREAD(mqtt_client_process, ev, data)
{
    static struct etimer activity_check_timer;

    PROCESS_BEGIN();

    // Initialize the contact list and memory block
    list_init(contacts_list);
    memb_init(&contacts_memb);
    memb_init(&mutual_contacts_mem);

    init_config();

    // Initialize and start the activity check timer
    etimer_set(&activity_check_timer, CLOCK_SECOND * 30); // Check activity every 30 seconds

    while (1)
    {
        PROCESS_YIELD();

        if (ev == PROCESS_EVENT_TIMER)
        {
            if (data == &fsm_periodic_timer)
            {
                state_machine();
            }
            else if (data == &activity_check_timer)
            {
                check_contact_activity();
                etimer_reset(&activity_check_timer); // Reset timer for next check
            }
        }
    }

    PROCESS_END();
}
