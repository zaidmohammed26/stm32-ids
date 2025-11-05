#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/capture.h>
#include <zephyr/net/promiscuous.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/ethernet.h>

#define C_PRIORITY 5
#define P_PRIORITY 6
#define L_PRIORITY 7
#define CAPTURE_STACK_SIZE 1024
#define PARSER_STACK_SIZE 2048
#define LOGGER_STACK_SIZE 1024
#define QUEUE_SIZE 128

K_THREAD_STACK_DEFINE(capture_stack_area,CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(parser_stack_area,PARSER_STACK_SIZE);
K_THREAD_STACK_DEFINE(logger_stack_area,LOGGER_STACK_SIZE);

struct k_thread c_data;
struct k_thread p_data;
struct k_thread l_data;
struct packet_data{
	void * reserved;
	struct net_pkt * pkt;
};
// int size = sizeof packet_data;
#define PACKET_DATA_SIZE sizeof(struct packet_data)

void capture_thread(void *p1,void *p2,void *p3);
void parser_thread(void *p1,void *p2,void *p3);
void logger_thread(void *p1,void *p2,void *p3);

K_MSGQ_DEFINE(packet_q, PACKET_DATA_SIZE, QUEUE_SIZE, __alignof(void *));


void capture_thread(void *p1,void *p2,void *p3){
	struct net_if *iface=net_if_get_default();
	if(iface==NULL){
		printf("interface not found\n");
		return;
	}
	net_dhcpv4_start(iface);
	int ret;
	// net_capture_
	ret = net_promisc_mode_on(iface);
	if (ret < 0) {
			if (ret == -EALREADY) {
					printf("Promiscuous mode already enabled\n");
			} else {
					printf("Cannot enable promiscuous mode for "
						"interface %p (%d)\n", iface, ret);
			}
	}
	while(1){
		struct net_pkt *pkt = net_promisc_mode_wait_data(K_FOREVER);
        if (pkt) {
			// net_pkt_ref(pkt);
			struct packet_data p_d;
			p_d.pkt=pkt;
			int r=k_msgq_put(&packet_q,&p_d,K_NO_WAIT);
			if(r<0){
				net_pkt_unref(pkt);
				
				printf("unable to put packet in queue\n");
				continue;
			}
			printf("captured a packet and added in queue\n");
			printf("queue size free = %d\n",k_msgq_num_free_get(&packet_q));
		}
		else{
			printf("unable to capture packet\n");
			k_sleep(K_FOREVER);
		}
		
	}
}

void parser_thread(void *p1, void *p2, void *p3)
{
    while (1) {
        struct packet_data p_d;
        int r = k_msgq_get(&packet_q, &p_d, K_FOREVER);
        if (r < 0) {
            printf("unable to get msg from queue\n");
            continue; // Go to next loop
        }

        struct net_pkt *pkt = p_d.pkt;
        struct net_eth_hdr *eth_hdr = NET_ETH_HDR(pkt);

        char src_addr_buf[NET_IPV6_ADDR_LEN], *src_addr;
        char dst_addr_buf[NET_IPV6_ADDR_LEN], *dst_addr;
        uint16_t dst_port = 0U, src_port = 0U;
        const char *proto;
        size_t len;

        // --- THIS IS THE CORRECTED LOGIC ---

        if (eth_hdr->type == htons(NET_ETH_PTYPE_IP)) {
            // It's IPv4. 
            
            // --- THIS IS THE FIX ---
            // Manually calculate the IP header's start position
            struct net_ipv4_hdr *ipv4_hdr = (struct net_ipv4_hdr *)((uint8_t *)eth_hdr + sizeof(struct net_eth_hdr));
            // --- END FIX ---

            switch (ipv4_hdr->proto) {
                case IPPROTO_TCP: proto = "TCP"; break;
                case IPPROTO_UDP: proto = "UDP"; break;
                case IPPROTO_ICMP: proto = "ICMP"; break;
                default: proto = "<unknown>"; break;
            }

            src_addr = net_addr_ntop(AF_INET, &ipv4_hdr->src, src_addr_buf, sizeof(src_addr_buf));
            dst_addr = net_addr_ntop(AF_INET, &ipv4_hdr->dst, dst_addr_buf, sizeof(dst_addr_buf));
            len = net_pkt_get_len(pkt);

            // Now, this printf will show the *correct* data!
            printf("IPV4: %s -> %s (%s, %d bytes)\n", src_addr, dst_addr, proto, len);

        } else if (eth_hdr->type == htons(NET_ETH_PTYPE_IPV6)) {
            
            // --- THIS IS THE FIX ---
            struct net_ipv6_hdr *ipv6_hdr = (struct net_ipv6_hdr *)((uint8_t *)eth_hdr + sizeof(struct net_eth_hdr));
            // --- END FIX ---

            switch (ipv6_hdr->nexthdr) {
                case IPPROTO_TCP: proto = "TCP"; break;
                case IPPROTO_UDP: proto = "UDP"; break;
                case IPPROTO_ICMPV6: proto = "ICMPv6"; break;
                default: proto = "<unknown>"; break;
            }

            src_addr = net_addr_ntop(AF_INET6, &ipv6_hdr->src, src_addr_buf, sizeof(src_addr_buf));
            dst_addr = net_addr_ntop(AF_INET6, &ipv6_hdr->dst, dst_addr_buf, sizeof(dst_addr_buf));
            len = net_pkt_get_len(pkt);
            
            printf("IPV6: %s -> %s (%s, %d bytes)\n", src_addr, dst_addr, proto, len);
        
        } else {
            // It's not IP. Print, release, and continue
            printf("Ignoring non-IP packet (Type: 0x%04x)\n", ntohs(eth_hdr->type));
        }

        // --- END OF CORRECTED LOGIC ---

        // Finally, release the packet
        net_pkt_unref(pkt);
    }
}

void logger_thread(void *p1,void *p2,void *p3){
	while(1){

		printf("logger\n");
		k_sleep(K_FOREVER);
	}
}


int main(){

	k_tid_t c_tid = k_thread_create(&c_data,capture_stack_area,K_THREAD_STACK_SIZEOF(capture_stack_area),capture_thread,NULL,NULL,NULL,C_PRIORITY,0,K_NO_WAIT);
	k_tid_t p_tid = k_thread_create(&p_data,parser_stack_area,K_THREAD_STACK_SIZEOF(parser_stack_area),parser_thread,NULL,NULL,NULL,P_PRIORITY,0,K_NO_WAIT);
	k_tid_t l_tid = k_thread_create(&l_data,logger_stack_area,K_THREAD_STACK_SIZEOF(logger_stack_area),logger_thread,NULL,NULL,NULL,L_PRIORITY,0,K_NO_WAIT);
	
	return 0;
}