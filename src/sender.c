#include "host.h"
#include <assert.h>
#include "switch.h"

//OLD host_get_next_expiring
struct timeval* host_get_next_expiring_timeval(Host* host) {
    // TODO: You should fill in this function so that it returns the 
    // timeval when next timeout should occur
    // 1) Check your send_window for the timeouts of the frames. 
    // 2) Return the timeout of a single frame. 
    // HINT: It's not the frame with the furtherst/latest timeout.

    struct timeval* next_expiring_timeout = NULL;
    struct send_window_slot* window_slot = NULL;

    for(int i = 0; i < glb_sysconfig.window_size; i++){
        window_slot = &(host->send_window[i]);
        if(next_expiring_timeout == NULL){
            next_expiring_timeout = window_slot->timeout;
        }
        else if(window_slot->timeout != NULL && timeval_usecdiff(next_expiring_timeout, window_slot->timeout) <= 0){
            next_expiring_timeout = window_slot->timeout;
        }
    }

    return next_expiring_timeout;
}

//NEW handle_incoming_acks
void handle_incoming_acks(Host* host, struct timeval curr_timeval) {

    long additional_ts = 0;
    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    // Num of acks received from each receiver
    uint8_t num_acks_received[glb_num_hosts]; 
    memset(num_acks_received, 0, glb_num_hosts); 

    // Num of duplicate acks received from each receiver this rtt
    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];     //PA1b
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts);

    //Receiver (sender of ack) host id
    uint8_t ack_src;

    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    while (incoming_frames_length > 0) {
        //Pop a node off the front of the link list and update the count
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* inframe = ll_inmsg_node->value;
        ack_src = inframe->src_id;

        //Compute CRC of incoming frame to know whether it is corrupted
        char* char_buf = convert_frame_to_char(inframe);
        uint8_t checksum = compute_crc8(char_buf);

        num_acks_received[ack_src] += 1;

        fprintf(stderr, "Sender %d handle_acks: Incoming ack %d, num acks = %d, dup acks = %d\n", host->id, inframe->seq_num, num_acks_received[ack_src], num_dup_acks_for_this_rtt[ack_src]);
        
        //Check if ACK is within sliding window
        int lar_diff = seq_num_diff(inframe->seq_num, host->sender[ack_src].lar);
        int lfs_diff = seq_num_diff(inframe->seq_num, host->sender[ack_src].lfs);
        if(checksum == 0 && lar_diff < 0 && lfs_diff >= 0){
            //Probably means this is not a duplicate
            num_dup_acks_for_this_rtt[ack_src] = 0;
            host->cc[ack_src].dup_acks = 0;
            //Slow start, non-dup ack
            if(host->cc[ack_src].state == cc_SS){
                if(host->cc[ack_src].cwnd <= host->cc[ack_src].ssthresh){
                    //Increment cwnd for every non-dup ack
                    host->cc[ack_src].cwnd += 1;
                }
                //cwnd > ssthresh
                else{
                    //Exit slow start and proceed to AIMD
                    host->cc[ack_src].state = cc_AIMD;
                }
            }
            //FRFT, ACK arrives that acknowledges new data
            if(host->cc[ack_src].state == cc_FRFT){
                host->cc[ack_src].cwnd = host->cc[ack_src].ssthresh;
                host->cc[ack_src].state = cc_AIMD;
            }
            //AIMD, non-dup ack
            if(host->cc[ack_src].state == cc_AIMD){
                //cwnd > ssthresh, not sure if this is necessary as this is a condition to enter AIMD?
                if(host->cc[ack_src].cwnd > host->cc[ack_src].ssthresh){
                    double incr = 1 / host->cc[ack_src].cwnd;
                    host->cc[ack_src].cwnd += incr;
                }
            }
            //Clear out frames covered in cumulative ACK from send window
            for(int i = 0; i < glb_sysconfig.window_size; i++){
                struct send_window_slot* window_slot = &(host->send_window[i]);
                if(window_slot->frame == NULL){
                    continue;
                }
                uint8_t seq_num = window_slot->frame->seq_num;
                if(seq_num_diff(seq_num, inframe->seq_num) >= 0 && window_slot->frame->dst_id == ack_src){
                    //Marks the final ack for that message
                    if(window_slot->frame->remaining_msg_bytes == 0){
                        host->curr_recv_id = host->id;
                        fprintf(stderr, "Sender %d handle_acks: Finished up msg with receiver %d\n", host->id, window_slot->frame->dst_id);
                    }
                    free(window_slot->frame);
                    free(window_slot->timeout);
                    window_slot->frame = NULL;
                    window_slot->timeout = NULL;
                    fprintf(stderr, "Sender %d handle_acks: Freed up frame %d\n", host->id, seq_num);
                }
            }
            //Shift frames to the left
            for(int i = 0; i < glb_sysconfig.window_size; i++){
                struct send_window_slot* window_slot = &(host->send_window[i]);
                if(window_slot->frame != NULL){
                    fprintf(stderr, "Sender %d handle_acks: Shifting window idx %d = frame %d\n", host->id, i, window_slot->frame->seq_num);
                    continue;
                }
                int right = i+1;
                while(right < glb_sysconfig.window_size){
                    if(host->send_window[right].frame == NULL){
                        right++;
                        continue;
                    }
                    else{
                        fprintf(stderr, "Sender %d handle_acks: Shifting window idx %d = frame %d\n", host->id, i, host->send_window[right].frame->seq_num);
                        window_slot->frame = host->send_window[right].frame;
                        window_slot->timeout = host->send_window[right].timeout;
                        host->send_window[right].frame = NULL;
                        host->send_window[right].timeout = NULL;
                        break;
                    }
                }
            }
            host->sender[ack_src].lar = inframe->seq_num;
        }
        else{
            fprintf(stderr, "Sender %d handle_acks: Ack %d not in sliding window, lar = %d, lfs = %d\n", host->id, inframe->seq_num, host->sender[ack_src].lar, host->sender[ack_src].lfs);
            //Probably means this is a duplicate
            num_dup_acks_for_this_rtt[ack_src] += 1;
            host->cc[ack_src].dup_acks += 1;
            //Increment cwnd by 1 for every dup ack past 3
            if(host->cc[ack_src].state == cc_FRFT){
                host->cc[ack_src].cwnd += 1;
            }
            if(host->cc[ack_src].dup_acks == 3){
                host->cc[ack_src].state = cc_FRFT;
                host->cc[ack_src].ssthresh = max_double(host->cc[ack_src].cwnd / 2, 2.0);
                host->cc[ack_src].cwnd = host->cc[ack_src].ssthresh + 3;
                //Immediately retransmit (and reset the timeout of) the frame with the next sequence number (should be at idx 0?)
                struct send_window_slot* window_slot = &(host->send_window[0]);
                if(window_slot->frame != NULL){
                    Frame* outframe = window_slot->frame;
                    Frame* outgoing_frame = calloc(1, sizeof(Frame));
                    assert(outgoing_frame);
                    outgoing_frame->remaining_msg_bytes = outframe->remaining_msg_bytes;
                    outgoing_frame->dst_id = outframe->dst_id;
                    outgoing_frame->src_id = outframe->src_id;
                    outgoing_frame->seq_num = outframe->seq_num;
                    strcpy(outgoing_frame->data, outframe->data);
                    outgoing_frame->crc = outframe->crc;

                    ll_append_node(&host->outgoing_frames_head, outgoing_frame);

                    struct timeval* next_timeout = malloc(sizeof(struct timeval));
                    memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
                    timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
                    additional_ts += 10000; //ADD ADDITIONAL 10ms
                    free(window_slot->timeout);
                    window_slot->timeout = next_timeout;

                    fprintf(stderr, "Sender %d handle_ack: Emergency retransmitting frame %d to receiver %d\n", host->id, outgoing_frame->seq_num, outgoing_frame->dst_id);
                }

            }
        }

        free(inframe);
        free(ll_inmsg_node);
    }

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num, num_acks_received[glb_sysconfig.host_recv_cc_id], num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]); 
        //fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num, num_acks_received[glb_sysconfig.host_recv_cc_id], host->cc[glb_sysconfig.host_recv_cc_id].dup_acks);
    }

}

//NEW handle_input_cmds
void handle_input_cmds(Host* host, struct timeval curr_timeval) {

    int input_cmd_length = ll_get_length(host->input_cmdlist_head);

    while(input_cmd_length > 0) {
        // Pop a node off and update the input_cmd_length
        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);

        // Cast to Cmd type and free up the memory for the node
        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);
 
        int msg_length = strlen(outgoing_cmd->message); // I got rid of +1 to account for null terminator
        int remaining_bytes = msg_length;
        uint8_t dst = outgoing_cmd->dst_id;
        
        for (int i = 0; i < msg_length; i += (FRAME_PAYLOAD_SIZE - 1)) {
            // Calculate size of current chunk
            size_t chunkSize = (i + FRAME_PAYLOAD_SIZE - 1 < msg_length) ? FRAME_PAYLOAD_SIZE - 1 : (msg_length - i);
            remaining_bytes -= chunkSize;

            // Process current chunk
            Frame* outgoing_frame = calloc(1, sizeof(Frame));
            assert(outgoing_frame);

            outgoing_frame->remaining_msg_bytes = remaining_bytes;
            outgoing_frame->dst_id = outgoing_cmd->dst_id;
            outgoing_frame->src_id = outgoing_cmd->src_id;
            outgoing_frame->seq_num = 0;

            strncpy(outgoing_frame->data, outgoing_cmd->message + i, chunkSize);
            outgoing_frame->data[chunkSize] = '\0';

            outgoing_frame->crc = 0;

            fprintf(stderr, "Sender %d handle_input_cmds: Chunk %d, remaining bytes %d, lfs = %d\n", host->id, i / (FRAME_PAYLOAD_SIZE - 1) , remaining_bytes, host->sender[dst].lfs);

            if(remaining_bytes == 0){
                fprintf(stderr, "Sender %d: finished command message - %s\n", host->id, outgoing_cmd->message);
            }

            ll_append_node(&host->buffered_outframes_head, outgoing_frame);
        }

        // At this point, we don't need the outgoing_cmd
        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}

//NEW handle_timedout_frames
void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

    // TODO: Handle frames that have timed out
    // Check your send_window for the frames that have timed out and set send_window[i]->timeout = NULL

    long additional_ts = 0;
    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    //If a frame times out, set ssthresh=cwnd / 2, set cwnd = 1, set state = slowstart, and set the timeout = NULL for all the frames in the window.
    struct send_window_slot* window_slot;
    for(int i = 0; i < glb_sysconfig.window_size; i++){
        window_slot = &(host->send_window[i]);
        if(window_slot->timeout != NULL && timeval_usecdiff(&curr_timeval, window_slot->timeout) <= 0){
            fprintf(stderr, "Sender %d handle_timedout: Frame %d has timed out, setting window to null\n", host->id, window_slot->frame->seq_num);
            //Frame has timed out, transition to slow start
            uint8_t recv_id = window_slot->frame->dst_id;
            host->cc[recv_id].ssthresh = host->cc[recv_id].cwnd / 2;
            host->cc[recv_id].cwnd = 1;
            host->cc[recv_id].state = cc_SS;
            //Mark all timeouts as null
            for(int i = 0; i < glb_sysconfig.window_size; i++){
                window_slot = &(host->send_window[i]);
                free(window_slot->timeout);
                window_slot->timeout = NULL;
            }
            break;
        }
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);
}

//NEW handle_outgoing_frames
void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {

    //TODO: Steps for handling outgoing frames
    //  1) Handle retransmitting timed out frames
    //      a) Retransmit any frames with NULL timeout and recalculate timeout
    //  2) Transmit any frames within the buffered outgoing frames queue
    //
    //Note: You can only send min(host->cc[recv_id].cwnd, glb_sysconfig.window_size) frames
    //      per handle_outgoing_frames/RTT. Additionally, you can only transmit new frames
    //      if there are open spots in the send window.

    long additional_ts = 0; 
    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    //int num_frames_sent = 0;

    //Retransmit frames that have timed out i.e. timeout = NULL
    struct send_window_slot* window_slot;
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        window_slot = &(host->send_window[i]);
        //Frame has timed out
        if(window_slot->frame != NULL && window_slot->timeout == NULL){
            Frame* outframe = window_slot->frame;
            Frame* outgoing_frame = calloc(1, sizeof(Frame));
            assert(outgoing_frame);

            outgoing_frame->remaining_msg_bytes = outframe->remaining_msg_bytes;
            outgoing_frame->dst_id = outframe->dst_id;
            outgoing_frame->src_id = outframe->src_id;
            outgoing_frame->seq_num = outframe->seq_num;
            strcpy(outgoing_frame->data, outframe->data);
            outgoing_frame->crc = outframe->crc;

            //Decide whether or not to retransmit i.e. num_frames_sent < min(host->cc[recv_id].cwnd, glb_sysconfig.window_size)
            int effective_window_size = (int)floor(min_double(host->cc[outframe->dst_id].cwnd, (double)glb_sysconfig.window_size));
            if(ll_get_length(host->outgoing_frames_head) < effective_window_size){ 
                ll_append_node(&host->outgoing_frames_head, outgoing_frame);
                //num_frames_sent++;
                fprintf(stderr, "Sender %d handle_outgoing: Retransmitting frame %d to receiver %d | effective window = %d, nfs = %d\n", host->id, outgoing_frame->seq_num, outgoing_frame->dst_id, effective_window_size, ll_get_length(host->outgoing_frames_head));

                struct timeval* new_timeout = malloc(sizeof(struct timeval));
                memcpy(new_timeout, &curr_timeval, sizeof(struct timeval)); 
                timeval_usecplus(new_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
                additional_ts += 10000; //ADD ADDITIONAL 10ms
                free(window_slot->timeout);
                window_slot->timeout = new_timeout;
            }
            else{
                fprintf(stderr, "Sender %d handle_outgoing: Not retransmitting because nfs = %d, window size = %d\n", host->id, ll_get_length(host->outgoing_frames_head), effective_window_size);
                free(outgoing_frame);
            } 
        } 
        else{
            fprintf(stderr, "Sender %d handle_outgoing: Window frame is null or timeout is not null\n", host->id);
        }
    }

    //Transmit new frames
    for (int i = 0; i < glb_sysconfig.window_size && ll_get_length(host->buffered_outframes_head) > 0; i++) {
        Frame* ll_peek = ll_peek_node(host->buffered_outframes_head);
        uint8_t recv_id = ll_peek->dst_id;
        int effective_window_size = (int)floor(min_double(host->cc[recv_id].cwnd, (double)glb_sysconfig.window_size));
        if (host->send_window[i].frame == NULL && ll_get_length(host->outgoing_frames_head) < effective_window_size
        && (host->curr_recv_id == host->id || host->curr_recv_id == recv_id)) {
            LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
            Frame* outframe = ll_outframe_node->value;
            uint8_t dst = outframe->dst_id; 

            Frame* outgoing_frame = calloc(1, sizeof(Frame));
            assert(outgoing_frame);

            outgoing_frame->remaining_msg_bytes = outframe->remaining_msg_bytes;
            outgoing_frame->dst_id = outframe->dst_id;
            outgoing_frame->src_id = outframe->src_id;
            //outgoing_frame->seq_num = outframe->seq_num;
            outgoing_frame->seq_num = ++(host->sender[dst].lfs);
            strcpy(outgoing_frame->data, outframe->data);
            char* char_buf = convert_frame_to_char(outgoing_frame);
            outgoing_frame->crc = compute_crc8(char_buf);

            Frame* outgoing_frame_copy = calloc(1, sizeof(Frame));
            assert(outgoing_frame_copy);

            outgoing_frame_copy->remaining_msg_bytes = outgoing_frame->remaining_msg_bytes;
            outgoing_frame_copy->dst_id = outgoing_frame->dst_id;
            outgoing_frame_copy->src_id = outgoing_frame->src_id;
            outgoing_frame_copy->seq_num = outgoing_frame->seq_num;
            strcpy(outgoing_frame_copy->data, outgoing_frame->data);
            outgoing_frame_copy->crc = outgoing_frame->crc;

            struct send_window_slot* window_slot = &(host->send_window[i % host->sender[dst].sws]);
            window_slot->frame = outgoing_frame_copy;

            ll_append_node(&host->outgoing_frames_head, outgoing_frame);
            //num_frames_sent++;
            host->curr_recv_id = outgoing_frame->dst_id;
            fprintf(stderr, "Sender %d handle_outgoing: New frame %d to receiver %d | effective window = %d, nfs = %d\n", host->id, outgoing_frame->seq_num, outgoing_frame->dst_id, effective_window_size, ll_get_length(host->outgoing_frames_head));
            
            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
            additional_ts += 10000; //ADD ADDITIONAL 10ms
            window_slot->timeout = next_timeout;

            free(outframe);
            free(ll_outframe_node);
        }
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);
    
    //NOTE:
    // Dont't worry about latest_timeout field for PA1a, but you need to understand what it does.
    // You may or may not use in PA1b when you implement fast recovery & fast retransmit in handle_incoming_acks(). 
    // If you choose to use it in PA1b, all you need to do is:

    // ****************************************
    // long additional_ts = 0; 
    // if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
    //     memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    // }

    //  YOUR FRFT CODE FOES HERE

    // memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    // timeval_usecplus(host->latest_timeout, additional_ts);
    // ****************************************


    // It essentially fixes the following problem:
    
    // 1) You send out 8 frames from sender0. 
    // Frame 1: curr_time + 0.09 + additional_ts(0.01) 
    // Frame 2: curr_time + 0.09 + additiona_ts (0.02) 
    // …

    // 2) Next time you send frames from sender0
    // Curr_time could be less than previous_curr_time + 0.09 + additional_ts. 
    // which means for example frame 9 will potentially timeout faster than frame 6 which shouldn’t happen. 

    // Latest timeout fixes that. 

}

// WE HIGHLY RECOMMEND TO NOT MODIFY THIS FUNCTION
void run_senders() {
    int sender_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, sender_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int sender_id = sender_order[i]; 
        struct timeval curr_timeval;

        gettimeofday(&curr_timeval, NULL);

        Host* host = &glb_hosts_array[sender_id]; 

        // Check whether anything has arrived
        int input_cmd_length = ll_get_length(host->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(host->incoming_frames_head);
        struct timeval* next_timeout = host_get_next_expiring_timeval(host); 
        
        // Conditions to "wake up" the host:
        //    1) Acknowledgement or new command
        //    2) Timeout      
        int incoming_frames_cmds = (input_cmd_length != 0) | (inframe_queue_length != 0); 
        long reached_timeout = (next_timeout != NULL) && (timeval_usecdiff(&curr_timeval, next_timeout) <= 0);

        host->awaiting_ack = 0; 
        host->active = 0; 
        host->csv_out = 0; 

        if (incoming_frames_cmds || reached_timeout) {
            host->round_trip_num += 1; 
            host->csv_out = 1; 
            
            // Implement this
            handle_input_cmds(host, curr_timeval); 
            // Implement this
            handle_incoming_acks(host, curr_timeval);
            // Implement this
            handle_timedout_frames(host, curr_timeval);
            // Implement this
            handle_outgoing_frames(host, curr_timeval); 
        }

        //Check if we are waiting for acks
        for (int j = 0; j < glb_sysconfig.window_size; j++) {
            if (host->send_window[j].frame != NULL) {
                host->awaiting_ack = 1; 
                break; 
            }
        }

        //Condition to indicate that the host is active 
        if (host->awaiting_ack || ll_get_length(host->buffered_outframes_head) > 0) {
            host->active = 1; 
        }
    }
}