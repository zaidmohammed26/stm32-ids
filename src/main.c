#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/capture.h>
#include <zephyr/net/promiscuous.h>

#define C_PRIORITY 5
#define P_PRIORITY 6
#define L_PRIORITY 7
#define CAPTURE_STACK_SIZE 1024
#define PARSER_STACK_SIZE 2048
#define LOGGER_STACK_SIZE 1024
#define QUEUE_SIZE 4
#define PACKET_DATA_SIZE 128

K_THREAD_STACK_DEFINE(capture_stack_area,CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(parser_stack_area,PARSER_STACK_SIZE);
K_THREAD_STACK_DEFINE(logger_stack_area,LOGGER_STACK_SIZE);

struct k_thread c_data;
struct k_thread p_data;
struct k_thread l_data;
struct packet_data{
	int a;
	int b;
};

void capture_thread(void *p1,void *p2,void *p3);
void parser_thread(void *p1,void *p2,void *p3);
void logger_thread(void *p1,void *p2,void *p3);

K_MSGQ_DEFINE(packet_q,PACKET_DATA_SIZE,QUEUE_SIZE,1);


void capture_thread(void *p1,void *p2,void *p3){
	struct net_if *iface=net_if_get_default();
	if(iface==NULL){
		printf("interface not found");
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
			net_pkt_ref(pkt);
			struct packet_data p_d={5,4};
			void * payload=&p_d;
			int r=k_msgq_put(&packet_q,payload,K_NO_WAIT);
			if(r<0){
				net_pkt_unref(pkt);
				
				printf("unable to put packet in queue\n");
				continue;
			}
			printf("captured a packet and added in queue\n");
			printf("queue size free = %d",k_msgq_num_free_get(&packet_q));
		}
		else{
			printf("unable to capture packet\n");
			k_sleep(K_FOREVER);
		}

        net_pkt_unref(pkt);
	}
}

void parser_thread(void *p1,void *p2,void *p3){
	while(1){
		void * payload;
		int r=k_msgq_get(&packet_q,payload,K_FOREVER);
		if(r<0){
			printf("unable to get msg from queue\n");
			
		}
		struct packet_data *p=(struct packet_data *) payload;
		printf("got a packet\n%d%d\n",p->a,p->b);

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