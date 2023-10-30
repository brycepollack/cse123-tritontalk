#include "host.h"
#include <assert.h>
#include "switch.h"

void handle_incoming_frames(Host* host) {
    // TODO: Suggested steps for handling incoming frames
    //    1) Dequeue the Frame from the host->incoming_frames_head
    //    2) Compute CRC of incoming frame to know whether it is corrupted
    //    3) If frame is corrupted, drop it and move on.
    //    4) Implement logic to check if the expected frame has come
    //    5) Implement logic to combine payload received from all frames belonging to a message
    //       and print the final message when all frames belonging to a message have been received.
    //    6) Implement the cumulative acknowledgement part of the sliding window protocol
    //    7) Append acknowledgement frames to the outgoing_frames_head queue
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    while (incoming_frames_length > 0) {
        // Pop a node off the front of the link list and update the count
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* inframe = ll_inmsg_node->value;
        uint8_t src = inframe->src_id;

        //Compute CRC of incoming frame to know whether it is corrupted
        char* char_buf = convert_frame_to_char(inframe);
        uint8_t checksum = compute_crc8(char_buf);

        //Compute sliding window checks
        int lfr_diff = seq_num_diff(inframe->seq_num, host->receiver[src].lfr);
        int laf_diff = seq_num_diff(inframe->seq_num, host->receiver[src].laf);

        fprintf(stderr, "Receiver %d handle_incoming_frames: sender = %d, lfr = %d, laf = %d, seq num = %d, checksum = %d\n", host->id, src, host->receiver[src].lfr, host->receiver[src].laf, inframe->seq_num, checksum);
        fprintf(stderr, "Receiver %d handle_incoming_frames: lfr diff = %d, laf diff = %d\n", host->id, lfr_diff, laf_diff);

        if(checksum == 0 && lfr_diff < 0 && laf_diff >= 0) {
            fprintf(stderr, "Receiver %d handle_incoming_frames: frame %d within sliding window\n", host->id, inframe->seq_num);
            //Frame is within sliding window
            int msg_fin = 0;
            Frame* incoming_frame = calloc(1, sizeof(Frame));
            assert(incoming_frame);

            incoming_frame->remaining_msg_bytes = inframe->remaining_msg_bytes;
            incoming_frame->dst_id = inframe->dst_id;
            incoming_frame->src_id = inframe->src_id;
            incoming_frame->seq_num = inframe->seq_num;
            //strcpy(incoming_frame->data, inframe->data);
            memcpy(incoming_frame->data, inframe->data, FRAME_PAYLOAD_SIZE);
            incoming_frame->crc = inframe->crc;

            struct receive_window_slot* window_slot = &(host->receive_window[src][inframe->seq_num % host->receiver[src].rws]);
            window_slot->frame = incoming_frame;
            window_slot->received = 1;

            //Sliding window logic
            if(seq_num_diff(host->receiver[src].lfr, inframe->seq_num) == 1){
                struct receive_window_slot* prev_slot = &(host->receive_window[src][((uint8_t)(host->receiver[src].lfr + 1) % host->receiver[src].rws)]);
                while(prev_slot->received == 1){
                    char* new_data = prev_slot->frame->data;
                    size_t new_msg_length = strlen(new_data) + strlen(host->receiver[src].msg) + 1;
                    char* temp_msg = realloc(host->receiver[src].msg, new_msg_length);
                    host->receiver[src].msg = temp_msg;
                    strcat(host->receiver[src].msg, prev_slot->frame->data);
                    ++(host->receiver[src].lfr);
                    ++(host->receiver[src].laf);
                    ++(host->receiver[src].seq_num_to_ack);

                    if(prev_slot->frame->remaining_msg_bytes == 0){
                        msg_fin = 1;
                        prev_slot->frame = NULL;
                        prev_slot->received = 0;
                        break;
                    }

                    prev_slot->frame = NULL;
                    prev_slot->received = 0;
                    prev_slot = &(host->receive_window[src][((uint8_t)(host->receiver[src].lfr + 1) % host->receiver[src].rws)]);

                    fprintf(stderr, "Receiver %d handle_incoming_frames: nef for sender %d, msg length is now %ld, lfr = %d, laf = %d, to_ack = %d, index = %d, prev_slot = %p\n", host->id, src, strlen(host->receiver[src].msg), host->receiver[src].lfr, host->receiver[src].laf, host->receiver[src].seq_num_to_ack, (host->receiver[src].lfr + 1) % host->receiver[src].rws, (void*)prev_slot);
                }

            }

            //Figure out if message should be printed and if so, print it
            if(msg_fin == 1){
                fprintf(stderr, "Receiver %d: Finished accumulating message from sender %d - %s\n", host->id, src, host->receiver[src].msg);
                printf("<RECV_%d>:[%s]\n", host->id, host->receiver[src].msg);
                msg_fin = 0;
                //probably need to do some cleaning up once we know message is done
                host->receiver[src].msg = malloc(1);
                *(host->receiver[src].msg) = 0;

                for(int i = 0; i < glb_sysconfig.window_size; i++){
                    host->receive_window[src][i].frame = NULL;
                    host->receive_window[src][i].received = 0;
                }

            }

            //send ack to src with id seq_num_to_ack
            Frame* ack_frame = calloc(1, sizeof(Frame));
            assert(ack_frame);

            ack_frame->remaining_msg_bytes = 0;
            ack_frame->dst_id = src;
            ack_frame->src_id = host->id;
            ack_frame->seq_num = host->receiver[src].seq_num_to_ack;
            //strcpy(ack_frame->data, inframe->data);
            memcpy(ack_frame->data, inframe->data, FRAME_PAYLOAD_SIZE);
            char* char_buf = convert_frame_to_char(ack_frame);
            ack_frame->crc = compute_crc8(char_buf);

            ll_append_node(&host->outgoing_frames_head, ack_frame);

        }
        else if(checksum == 0){
            //send ack to src with id seq_num_to_ack
            Frame* ack_frame = calloc(1, sizeof(Frame));
            assert(ack_frame);

            ack_frame->remaining_msg_bytes = 0;
            ack_frame->dst_id = src;
            ack_frame->src_id = host->id;
            ack_frame->seq_num = host->receiver[src].seq_num_to_ack;
            //strcpy(ack_frame->data, inframe->data);
            memcpy(ack_frame->data, inframe->data, FRAME_PAYLOAD_SIZE);
            char* char_buf = convert_frame_to_char(ack_frame);
            ack_frame->crc = compute_crc8(char_buf);

            ll_append_node(&host->outgoing_frames_head, ack_frame);

            fprintf(stderr, "Receiver %d handle_incoming_frames: not in swp, lfr = %d, laf = %d, seq num = %d\n", host->id, host->receiver[src].lfr, host->receiver[src].laf, inframe->seq_num);
        }
        else{
            fprintf(stderr, "Receiver %d handle_incoming_frames: checksum error\n", host->id);
        }

        free(inframe);
        free(ll_inmsg_node);

    }

}

void run_receivers() {
    int recv_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, recv_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int recv_id = recv_order[i]; 
        handle_incoming_frames(&glb_hosts_array[recv_id]); 
    }
}