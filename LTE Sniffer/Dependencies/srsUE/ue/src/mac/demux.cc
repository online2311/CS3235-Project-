/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2015 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsUE library.
 *
 * srsUE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsUE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */


#define Error(fmt, ...)   log_h->error_line(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define Warning(fmt, ...) log_h->warning_line(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define Info(fmt, ...)    log_h->info_line(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define Debug(fmt, ...)   log_h->debug_line(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#include "mac/mac.h"
#include "mac/demux.h"


namespace srsue {
    
demux::demux() : mac_msg(20), pending_mac_msg(20)
{
  for (int i=0;i<NOF_HARQ_PID;i++) {
    pdu_q[i].init(NOF_BUFFER_PDUS, MAX_PDU_LEN);
  }
}

void demux::init(phy_interface* phy_h_, rlc_interface_mac *rlc_, srslte::log* log_h_, srslte::timers* timers_db_)
{
  phy_h     = phy_h_; 
  log_h     = log_h_; 
  rlc       = rlc_;  
  timers_db = timers_db_;
}

void demux::set_uecrid_callback(bool (*callback)(void*,uint64_t), void *arg) {
  uecrid_callback     = callback;
  uecrid_callback_arg = arg; 
}

bool demux::get_uecrid_successful() {
  return is_uecrid_successful;
}

uint8_t* demux::request_buffer(uint32_t pid, uint32_t len)
{  
  uint8_t *buff = NULL; 
  if (pid < NOF_HARQ_PID) {
    if (len < MAX_PDU_LEN) {
      if (pdu_q[pid].pending_msgs() > 0.75*pdu_q[pid].max_msgs()) {
        log_h->console("Warning buffer PID=%d: Occupation is %.1f%% \n", 
                      pid, (float) 100*pdu_q[pid].pending_msgs()/pdu_q[pid].max_msgs());
      }
      buff = (uint8_t*) pdu_q[pid].request();
      if (!buff) {
        fprintf(stderr, "Buffer full for PID=%d\n", pid);
        exit(0); 
      }      
    } else {
      Error("Requested too large buffer for PID=%d. Requested %d bytes, max length %d bytes\n", 
            pid, len, MAX_PDU_LEN);
    }
  } else if (pid == NOF_HARQ_PID) {
    buff = bcch_buffer;
  } else {
    Error("Requested buffer for invalid PID=%d\n", pid);
  }
  return buff; 
}

/* Demultiplexing of MAC PDU associated with a Temporal C-RNTI. The PDU will 
 * remain in buffer until demultiplex_pending_pdu() is called. 
 * This features is provided to enable the Random Access Procedure to decide 
 * wether the PDU shall pass to upper layers or not, which depends on the 
 * Contention Resolution result. 
 * 
 * Warning: this function does some processing here assuming ACK deadline is not an 
 * issue here because Temp C-RNTI messages have small payloads
 */
void demux::push_pdu_temp_crnti(uint32_t pid, uint8_t *buff, uint32_t nof_bytes) 
{
  if (pid < NOF_HARQ_PID) {
    if (nof_bytes > 0) {
      // Unpack DLSCH MAC PDU 
      pending_mac_msg.init_rx(nof_bytes);
      pending_mac_msg.parse_packet(buff);
      
      // Look for Contention Resolution UE ID 
      is_uecrid_successful = false; 
      while(pending_mac_msg.next() && !is_uecrid_successful) {
        if (pending_mac_msg.get()->ce_type() == sch_subh::CON_RES_ID) {
          Debug("Found Contention Resolution ID CE\n");
          is_uecrid_successful = uecrid_callback(uecrid_callback_arg, pending_mac_msg.get()->get_con_res_id());
        }
      }
      
      pending_mac_msg.reset();
      
      Debug("Saved MAC PDU with Temporal C-RNTI in buffer\n");
      
      if (!pdu_q[pid].push(nof_bytes)) {
        Warning("Full queue %d when pushing MAC PDU %d bytes\n", pid, nof_bytes);
      }
    } else {
      Warning("Trying to push PDU with payload size zero\n");
    }
  } else {
    Error("Pushed buffer for invalid PID=%d\n", pid);
  } 
}

/* Demultiplexing of logical channels and dissassemble of MAC CE 
 * This function enqueues the packet and returns quicly because ACK 
 * deadline is important here. 
 */ 
void demux::push_pdu(uint32_t pid, uint8_t *buff, uint32_t nof_bytes)
{
  if (pid < NOF_HARQ_PID) {    
    if (nof_bytes > 0) {
      if (!pdu_q[pid].push(nof_bytes)) {
        Warning("Full queue %d when pushing MAC PDU %d bytes\n", pid, nof_bytes);
      }
    } else {
      Warning("Trying to push PDU with payload size zero\n");
    }
  } else if (pid == NOF_HARQ_PID) {
    /* Demultiplexing of MAC PDU associated with SI-RNTI. The PDU passes through 
    * the MAC in transparent mode. 
    * Warning: In this case function sends the message to RLC now, since SI blocks do not 
    * require ACK feedback to be transmitted quickly. 
    */
    Debug("Pushed BCCH MAC PDU in transparent mode\n");
    rlc->write_pdu_bcch_dlsch(buff, nof_bytes);
  } else {
    Error("Pushed buffer for invalid PID=%d\n", pid);
  }  
}

bool demux::process_pdus()
{
  bool have_data = false; 
  for (int i=0;i<NOF_HARQ_PID;i++) {
    uint8_t *buff = NULL;
    uint32_t len  = 0; 
    uint32_t cnt  = 0; 
    do {
      buff = (uint8_t*) pdu_q[i].pop(&len);
      if (buff) {
        process_pdu(buff, len);
        pdu_q[i].release();
        cnt++;
        have_data = true;
      }
    } while(buff);
    if (cnt > 4) {
      log_h->console("Warning dispatched %d packets for PID=%d\n", cnt, i);
    }
  }
  return have_data; 
}

void demux::process_pdu(uint8_t *mac_pdu, uint32_t nof_bytes)
{
  // Unpack DLSCH MAC PDU 
  mac_msg.init_rx(nof_bytes);
  mac_msg.parse_packet(mac_pdu);

  process_sch_pdu(&mac_msg);
  //srslte_vec_fprint_byte(stdout, mac_pdu, nof_bytes);
  Debug("MAC PDU processed\n");
}

void demux::process_sch_pdu(sch_pdu *pdu_msg)
{  
  while(pdu_msg->next()) {
    if (pdu_msg->get()->is_sdu()) {
      // Route logical channel 
      Info("Delivering PDU for lcid=%d, %d bytes\n", pdu_msg->get()->get_sdu_lcid(), pdu_msg->get()->get_payload_size());
      rlc->write_pdu(pdu_msg->get()->get_sdu_lcid(), pdu_msg->get()->get_sdu_ptr(), pdu_msg->get()->get_payload_size());      
    } else {
      // Process MAC Control Element
      if (!process_ce(pdu_msg->get())) {
        Warning("Received Subheader with invalid or unkonwn LCID\n");
      }
    }
  }      
}

bool demux::process_ce(sch_subh *subh) {
  switch(subh->ce_type()) {
    case sch_subh::CON_RES_ID:
      // Do nothing
      break;
    case sch_subh::TA_CMD:
      phy_h->set_timeadv(subh->get_ta_cmd());
      
      // Start or restart timeAlignmentTimer
      timers_db->get(mac::TIME_ALIGNMENT)->reset();
      timers_db->get(mac::TIME_ALIGNMENT)->run();
      Info("Received time advance command %d\n", subh->get_ta_cmd());
      break;
    case sch_subh::PADDING:
      break;
    default:
      Error("MAC CE 0x%x not supported\n", subh->ce_type());
      break;
  }
  return true; 
}


}
