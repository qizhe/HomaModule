/* Copyright (c) 2019-2020, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* This file contains definitions that are shared across the files
 * that implement Homa for Linux.
 */

#ifndef _HOMA_IMPL_H
#define _HOMA_IMPL_H

#include <linux/audit.h>
#include <linux/icmp.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <linux/socket.h>
#include <linux/indirect_call_wrapper.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/inet_common.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,16,0)
typedef unsigned int __poll_t;
#endif

#ifdef __UNIT_TEST__
#define spin_unlock mock_spin_unlock
extern void mock_spin_unlock(spinlock_t *lock);

#define get_cycles mock_get_cycles
extern cycles_t mock_get_cycles(void);

#define signal_pending(xxx) mock_signal_pending
extern int mock_signal_pending;

#define rcu_read_lock mock_rcu_read_lock
extern void mock_rcu_read_lock(void);

#define rcu_read_unlock mock_rcu_read_unlock
extern void mock_rcu_read_unlock(void);

#undef current
#define current current_task

#undef NR_CPUS
#define NR_CPUS 8
#endif

#include "homa.h"
#include "timetrace.h"

/* Forward declarations. */
struct homa_sock;
struct homa_rpc;
struct homa;

/* Declarations used in this file, so they can't be made at the end. */
extern void     homa_grantable_lock_slow(struct homa *homa);
extern void     homa_rpc_lock_slow(struct homa_rpc *rpc);
extern void     homa_sock_lock_slow(struct homa_sock *hsk);
extern void     homa_throttle_lock_slow(struct homa *homa);

/**
 * enum homa_packet_type - Defines the possible types of Homa packets.
 * 
 * See the xxx_header structs below for more information about each type.
 */
enum homa_packet_type {
	DATA               = 20,
	// RESUE for DCACP
	GRANT              = 21,

	RESEND             = 22,
	RESTART            = 23,
	BUSY               = 24,
	CUTOFFS            = 25,
	FREEZE             = 26,
	//For DCACP
	NOTIFICATION	   = 27,
	RTS                = 28,
	ACCEPT			   = 29,
	DCACP_GRANT			   = 30,

	BOGUS              = 31,      /* Used only in unit tests. */
	/* If you add a new type here, you must also do the following:
	 * 1. Change BOGUS so it is the highest opcode
	 * 2. Add support for the new opcode in homa_print_packet,
	 *    homa_print_packet_short, homa_symbol_for_type, and mock_skb_new.q
	 */
};

/**
 * define HOMA_MAX_MESSAGE_SIZE - Largest permissible message size, in bytes.
 */
#define HOMA_MAX_MESSAGE_SIZE 1000000

/** define HOMA_IPV4_HEADER_LENGTH - Size of IP header (V4). */
#define HOMA_IPV4_HEADER_LENGTH 20

/**
 * define HOMA_SKB_EXTRA - How many bytes of additional space to allow at the
 * beginning of each sk_buff, before the IP header. This includes room for a
 * VLAN header and also includes some extra space, "just to be safe" (not
 * really sure if this is needed).
 */
#define HOMA_SKB_EXTRA 40

/** define HOMA_VLAN_HEADER - Number of bytes in an Ethernet VLAN header. */
#define HOMA_VLAN_HEADER 20

/**
 * define HOMA_ETH_OVERHEAD - Number of bytes per Ethernet packet for CRC,
 * preamble, and inter-packet gap.
 */
#define HOMA_ETH_OVERHEAD 24

/**
 * define HOMA_MAX_HEADER - Largest allowable Homa header.  All Homa packets
 * must be at least this long.
 */
#define HOMA_MAX_HEADER 64

/**
 * define ETHERNET_MAX_PAYLOAD - A maximum length of an Ethernet packet,
 * excluding preamble, frame delimeter, VLAN header, CRC, and interpacket gap;
 * i.e. all of this space is available for Homa.
 */
#define ETHERNET_MAX_PAYLOAD 1500

/**
 * define HOMA_MAX_PRIORITIES - The maximum number of priority levels that
 * Homa can use (the actual number can be restricted to less than this at
 * runtime). Changing this value will affect packet formats.
 */
#define HOMA_MAX_PRIORITIES 8

#define sizeof32(type) ((int) (sizeof(type)))

/**
 * struct common_header - Wire format for the first bytes in every Homa
 * packet. This must partially match the format of a TCP header so that
 * Homa can piggyback on TCP segmentation offload (and possibly other
 * features, such as RSS).
 */
struct common_header {
	/**
	 * @sport: Port on source machine from which packet was sent.
	 * Must be in the same position as in a TCP header.
	 */
	__be16 sport;
	
	/**
	 * @dport: Port on destination that is to receive packet. Must be
	 * in the same position as in a TCP header.
	 */
	__be16 dport;
	
	/**
	 * @unused1: corresponds to the sequence number field in TCP headers;
	 * must not be used by Homa, in case it gets incremented during TCP
	 * offload.
	 */
	__be32 unused1;
	
	__be32 unused2;
	
	/**
	 * @doff: High order 4 bits holds the number of 4-byte chunks in a
	 * data_header (low-order bits unused). Used only for DATA packets;
	 * must be in the same position as the data offset in a TCP header.
	 */
	__u8 doff;

	/** @type: One of the values of &enum packet_type. */
	__u8 type;
	
	/**
	 * @gro_count: value on the wire is undefined. Used only by
	 * homa_offload.c (it counts the total number of packets aggregated
	 * into this packet, including the top-level packet).
	 */
	__u16 gro_count;
	
	/**
	 * @checksum: not used by Homa, but must occupy the same bytes as
	 * the checksum in a TCP header (TSO may modify this?).*/
	__be16 checksum;
	
	/**
	 * @priority: the priority at which the packet was set; used
	 * only for debugging.
	 */
	__u16 priority;
		
	/**
	 * @id: Identifier for the RPC associated with this packet; must
	 * be unique among all those issued from the client port. Stored
	 * in client host byte order.
	 */
	__be64 id;
} __attribute__((packed));

/** 
 * struct data_segment - Wire format for a chunk of data that is part of
 * a DATA packet. A single sk_buff can hold multiple data_segments in order
 * to enable send and receive offload (the idea is to carry many network
 * packets of info in a single traversal of the Linux networking stack).
 * A DATA sk_buff contains a data_header followed by any number of
 * data_segments.
 */
struct data_segment {
	/**
	 * @offset: Offset within message of the first byte of data in
	 * this segment. Segments within an sk_buff are not guaranteed
	 * to be in order.
	 */
	__be32 offset;
	
	/** @segment_length: Number of bytes of data in this segment. */
	__be32 segment_length;
	
	/** @data: the payload of this segment. */
	char data[0];
} __attribute__((packed));

/* struct data_header - Overall header format for a DATA sk_buff, which
 * contains this header followed by any number of data_segments.
 */
struct data_header {
	struct common_header common;
	
	/** @message_length: Total #bytes in the *message* */
	__be32 message_length;
	
	/**
	 * @incoming: The receiver can expect the sender to send all of the
	 * bytes in the message up to at least this offset (exclusive),
	 * even without additional grants. This includes unscheduled
	 * bytes, granted bytes, plus any additional bytes the sender
	 * transmits unilaterally (e.g., to send batches, such as with GSO).
	 */
	__be32 incoming;
	
	/**
	 * @cutoff_version: The cutoff_version from the most recent
	 * CUTOFFS packet that the source of this packet has received
	 * from the destination of this packet, or 0 if the source hasn't
	 * yet received a CUTOFFS packet.
	 */
	__be16 cutoff_version;

	/**
	 * @retransmit: 1 means this packet was sent in response to a RESEND
	 * (it has already been sent previously).
	 */
	__u8 retransmit;
	
	__u8 pad;
	
	/** @seg: First of possibly many segments */
	struct data_segment seg;
} __attribute__((packed));
_Static_assert(sizeof(struct data_header) <= HOMA_MAX_HEADER,
		"data_header too large");

_Static_assert(((sizeof(struct data_header) - sizeof(struct data_segment))
		& 0x3) == 0,
		" data_header length not a multiple of 4 bytes (required "
		"for TCP/TSO compatibility");

/**
 * struct grant_header - Wire format for GRANT packets, which are sent by
 * the receiver back to the sender to indicate that the sender may transmit
 * additional bytes in the message.
 */
struct grant_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	/**
	 * @offset: Byte offset within the message.
	 * 
	 * The sender should now transmit all data up to (but not including)
	 * this offset ASAP, if it hasn't already.
	 */
	__be32 offset;
	
	/**
	 * @priority: The sender should use this priority level for all future
	 * MESSAGE_FRAG packets for this message, until a GRANT is received
	 * with higher offset. Larger numbers indicate higher priorities.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct grant_header) <= HOMA_MAX_HEADER,
		"grant_header too large");

/**
 * struct resend_header - Wire format for RESEND packets.
 *
 * A RESEND is sent by the receiver when it believes that message data may
 * have been lost in transmission (or if it is concerned that the sender may
 * have crashed). The receiver should resend the specified portion of the
 * message, even if it already sent it previously.
 */
struct resend_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	/**
	 * @offset: Offset within the message of the first byte of data that
	 * should be retransmitted.
	 */
	__be32 offset;
	
	/**
	 * @length: Number of bytes of data to retransmit; this could specify
	 * a range longer than the total message size.
	 */
	__be32 length;
	
	/**
	 * @priority: Packet priority to use.
	 * 
	 * The sender should transmit all the requested data using this
	 * priority.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct resend_header) <= HOMA_MAX_HEADER,
		"resend_header too large");

/**
 * struct restart_header - Wire format for RESTART packets.
 *
 * A RESTART is sent by a server when it receives a RESEND request for
 * an RPC that is unknown to it. This can occur in two situations. The first
 * situation is when all of the request packets sent by the client were lost.
 * The second situation is when the server received the entire request,
 * processed it, transmitted the response, and discarded its RPC state, but
 * some of the response packets were lost. A RESTART request indicates to the
 * client that it should restart the RPC from the beginning, discarding any
 * partial response received so far and reinitiating transmission of the
 * request. Note that this can cause an RPC to be executed multiple times
 * on the server; this is explicitly allowed by the Homa protocol.
 */
struct restart_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
} __attribute__((packed));
_Static_assert(sizeof(struct restart_header) <= HOMA_MAX_HEADER,
		"restart_header too large");

/**
 * struct busy_header - Wire format for BUSY packets.
 * 
 * These packets tell the recipient that the sender is still alive (even if
 * it isn't sending data expected by the recipient).
 */
struct busy_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
} __attribute__((packed));
_Static_assert(sizeof(struct busy_header) <= HOMA_MAX_HEADER,
		"busy_header too large");

/**
 * struct cutoffs_header - Wire format for CUTOFFS packets.
 * 
 * These packets tell the recipient how to assign priorities to
 * unscheduled packets.
 */
struct cutoffs_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	/**
	 * @unsched_cutoffs: priorities to use for unscheduled packets
	 * sent to the sender of this packet. See documentation for
	 * @homa.unsched_cutoffs for the meanings of these values.
	 */
	__be32 unsched_cutoffs[HOMA_MAX_PRIORITIES];
	
	/**
	 * @cutoff_version: unique identifier associated with @unsched_cutoffs.
	 * Must be included in future DATA packets sent to the sender of
	 * this packet.
	 */
	__be16 cutoff_version;
} __attribute__((packed));
_Static_assert(sizeof(struct cutoffs_header) <= HOMA_MAX_HEADER,
		"cutoffs_header too large");


/**
 * struct freeze_header - Wire format for FREEZE packets.
 * 
 * These packets tell the recipient to freeze its timetrace; used
 * for debugging.
 */
struct freeze_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
} __attribute__((packed));
_Static_assert(sizeof(struct freeze_header) <= HOMA_MAX_HEADER,
		"freeze_header too large");


// ---

/**
 * struct notification_header - Wire format for NOTIFICATION packets.
 * 
 * send DCACP NOTIFICATION packets for matching from the sender to the receiver
 */
struct notification_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	/**
	 * @flow_id: flow id should be unique within one socket.
	 */
	__be32 message_id;
	/**
	 * @flow_size: Byte flow_size of the message
	 */
	__be64 message_size;


	
	/**
	 * @priority: The sender should use this priority level for all future
	 * MESSAGE_FRAG packets for this message, until a GRANT is received
	 * with higher offset. Larger numbers indicate higher priorities.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct notification_header) <= HOMA_MAX_HEADER,
		"notification_header too large");

/**
 * struct rts_header - Wire format for RTS packets.
 * 
 * send DCACP RTS packets for matching from the receiver to the sender
 */
struct rts_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	/**
	 * @flow_size: Byte flow_size of the message.
	 */
	__be64 message_size;
	
	/**
	 * @priority: The sender should use this priority level for all future
	 * MESSAGE_FRAG packets for this message, until a GRANT is received
	 * with higher offset. Larger numbers indicate higher priorities.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct rts_header) <= HOMA_MAX_HEADER,
		"rts_header too large");

/**
 * struct accept_header - Wire format for ACCEPT packets.
 * 
 * send DCACP ACCEPT packets for matching from the receiver to the sender
 */
struct accept_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	// *
	//  * @offset: Byte offset within the message.
	//  * 
	//  * The sender should now transmit all data up to (but not including)
	//  * this offset ASAP, if it hasn't already.
	 
	// __be32 offset;
	
	/**
	 * @priority: The sender should use this priority level for all future
	 * MESSAGE_FRAG packets for this message, until a GRANT is received
	 * with higher offset. Larger numbers indicate higher priorities.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct accept_header) <= HOMA_MAX_HEADER,
		"accept_header too large");

/**
 * struct dcacp_grant_header - Wire format for DCACP_GRANT_HEADER packets.
 * 
 * send DCACP TOKEN packets for matching from the receiver to the sender
 */
struct dcacp_grant_header {
	/** @common: Fields common to all packet types. */
	struct common_header common;
	
	/**
	 * @message_size: Remaining flow size of  the message.
	 * 
	 * The sender should now transmit all data up to (but not including)
	 * this offset ASAP, if it hasn't already.
	 */
	__be64 message_size;
	
	/**
	 * @priority: The sender should use this priority level for all future
	 * MESSAGE_FRAG packets for this message, until a GRANT is received
	 * with higher offset. Larger numbers indicate higher priorities.
	 */
	__u8 priority;
} __attribute__((packed));
_Static_assert(sizeof(struct dcacp_grant_header) <= HOMA_MAX_HEADER,
		"dcacp_grant_header too large");

/**
 * struct homa_message_out - Describes a message (either request or response)
 * for which this machine is the sender.
 */
struct homa_message_out {
	/** @length: Total bytes in message (excluding headers).  A value
	 * less than 0 means this structure is uninitialized and therefore
	 * not in use.*/
	int length;
	
	/**
	 * @packets: singly-linked list of all packets in message, linked
	 * using homa_next_skb. The list is in order of offset in the message
	 * (offset 0 first); each sk_buff can potentially contain multiple
	 * data_segments, which will be split into separate packets by GSO.
	 */
	struct sk_buff *packets;
	
	/**
	 * @num_skbs:  Total number of buffers in @packets. Will be 0 if
	 * @length is less than 0.
	 */
	int num_skbs;
	
	/**
	 * @next_packet: Pointer within @request of the next packet to transmit.
	 * 
	 * All packets before this one have already been sent. NULL means
	 * entire message has been sent.
	 */
	struct sk_buff *next_packet;
	
	/**
	 * @unscheduled: Initial bytes of message that we'll send
	 * without waiting for grants. May be larger than @length;
	 */
	int unscheduled;
	
	/** 
	 * @granted: Total number of bytes we are currently permitted to
	 * send, including unscheduled bytes; must wait for grants before
	 * sending bytes at or beyond this position. Never larger than
	 * @length.
	 */
	int granted;
	
	/** @priority: Priority level to use for future scheduled packets. */
	__u8 sched_priority;
};

/**
 * struct homa_message_in - Holds the state of a message received by
 * this machine; used for both requests and responses. 
 */
struct homa_message_in {
	/**
	 * @total_length: Size of the entire message, in bytes. A value
	 * less than 0 means this structure is uninitialized and therefore
	 * not in use.
	 */
	int total_length;
	
	/**
	 * @packets: DATA packets received for this message so far. The list
	 * is sorted in order of offset (head is lowest offset), but
	 * packets can be received out of order, so there may be times
	 * when there are holes in the list. Packets in this list contain
	 * exactly one data_segment.
	 */
	struct sk_buff_head packets;
	
	/**
	 * @num_skbs:  Total number of buffers in @packets. Will be 0 if
	 * @total_length is less than 0.
	 */
	int num_skbs;
	
	/**
	 * @bytes_remaining: Amount of data for this message that has
	 * not yet been received; will determine the message's priority.
	 */
	int bytes_remaining;

        /**
	 * @incoming: Total # of bytes of the message that the sender will
	 * transmit without additional grants. Never larger than @total_length.
	 */
        int incoming;
	
	/** @priority: Priority level to include in future GRANTS. */
	int priority;
	
	/**
	 * @scheduled: True means some of the bytes of this message
	 * must be scheduled with grants.
	 */
	bool scheduled;
	
	/**
		TODO: homa_sock 
	*/
		
	/**
		TODO: homa_peer 
	*/


	/**
	 * @possibly_in_grant_queue: True means this RPC may be linked
	 * into homa->grantable_rpcs. Zero means it can't possibly be in
	 * the list, so no need to check (which means acquiring a global
	 * lock) when cleaning up the RPC.
	 */
	bool possibly_in_grant_queue;
};

/**
 * struct homa_interest - Indicates that a blocked thread wishes to receive an
 * incoming request or response message.
 */
struct homa_interest {
	/**
	 * @thread: thread that would like to receive a message. Will get
	 * woken up when a suitable message becomes available.
	 */
	struct task_struct *thread;
	
	/**
	 * @id: Id of the RPC that was found, or zero if none. This variable
	 * is used for synchronization, and must be set after the variables
	 * below it. This variable and the others below are used later to
	 * look up and lock the RPC. It isn't safe to pass the RPC itself:
	 * the locking rules create a window where the RPC could be deleted.
	 */
	atomic_long_t id;
	
	/**
	 * @peer_addr: IP address of the peer for the matching RPC. Valid
	 * only if @id is nonzero.
	 */
	__be32 peer_addr;
	
	/**
	 * @peer_port: Port of the peer for the matching RPC. Valid
	 * only if @id is nonzero.
	 */
	__u16 peer_port;
	
	/**
	 * @is_client: True means that the matching RPC is a client RPC. Valid
	 * only if @id is nonzero.
	 */
	bool is_client;
	
	/**
	 * @reg_rpc: RPC whose @interest field points here, or
	 * NULL if none.
	 */
	struct homa_rpc *reg_rpc;
	
	/**
	 * @req_links: For linking this object into
	 * &homa_sock.request_interests.
	 */
	struct list_head request_links;
	
	/**
	 * @req_links: For linking this object into
	 * &homa_sock.request_interests.
	 */
	struct list_head response_links;
};

/**
 * struct homa_rpc - One of these structures exists for each active
 * RPC. The same structure is used to manage both outgoing RPCs on
 * clients and incoming RPCs on servers.
 */
struct homa_rpc {
	/** @hsk:  Socket that owns the RPC. */
	struct homa_sock *hsk;
	
	/** @lock: Used to synchronize modifications to this structure;
	 * points to the lock in hsk->client_rpc_buckets or
	 * hsk->server_rpc_buckets.
	 */
	struct spinlock *lock;
	
	/**
	 * @state: The current state of this RPC:
	 * 
	 * @RPC_OUTGOING:     The RPC is waiting for @msgout to be transmitted
	 *                    to the peer.
	 * @RPC_INCOMING:     The RPC is waiting for data @msgin to be received
	 *                    from the peer; at least one packet has already
	 *                    been received.
	 * @RPC_READY:        @msgin is now complete; the next step is for
	 *                    the message to be read from the socket by the
	 *                    application.
	 * @RPC_IN_SERVICE:   Used only for server RPCs: the request message
	 *                    has been read from the socket, but the response
	 *                    message has not yet been presented to the kernel.
	 * @RPC_DEAD:         RPC has been deleted and is waiting to be
	 *                    reaped. In some cases, information in the RPC
	 *                    structure may be accessed in this state.
	 * 
	 * Client RPCs pass through states in the following order:
	 * RPC_OUTGOING, RPC_INCOMING, RPC_READY, RPC_DEAD.
	 * 
	 * Server RPCs pass through states in the following order:
	 * RPC_INCOMING, RPC_READY, RPC_IN_SERVICE, RPC_OUTGOING, RPC_DEAD.
	 */
	enum {
		RPC_OUTGOING            = 5,
		RPC_INCOMING            = 6,
		RPC_READY               = 7,
		RPC_IN_SERVICE          = 8,
		RPC_DEAD                = 9
	} state;
	
	/** @is_client: True means this is a client RPC, false means server. */
	bool is_client;
	
	/** 
	 * @dont_reap: True means data is still being copied out of the
	 * RPC to a receiver, so it isn't safe to reap it yet.
	 */
	bool dont_reap;
	
	/**
	 * @peer: Information about the other machine (the server, if
	 * this is a client RPC, or the client, if this is a server RPC).
	 */
	struct homa_peer *peer;
	
	/** @dport: Port number on @peer that will handle packets. */
	__u16 dport;
	
	/**
	 * @id: Unique identifier for the RPC among all those issued
	 * from its port. Selected by the client.
	 */
	__u64 id;
	
	/**
	 * @error: Only used on clients. If nonzero, then the RPC has
	 * failed and the value is a negative errno that describes the
	 * problem.
	 */
	int error;
	
	/**
	 * @msgin: Information about the message we receive for this RPC
	 * (for server RPCs this is the request, for client RPCs this is the
	 * response).
	 */
	struct homa_message_in msgin;
	
	/** 
	 * @msgout: Information about the message we send for this RPC
	 * (for client RPCs this is the request, for server RPCs this is the
	 * response).
	 */
	struct homa_message_out msgout;
	
	/**
	 * @hash_links: Used to link this object into a hash bucket for
	 * either @hsk->client_rpc_buckets (for a client RPC), or
	 * @hsk->server_rpc_buckets (for a server RPC).
	 */
	struct hlist_node hash_links;
	
	/**
	 * @active_links: For linking this object into @hsk->active_rpcs.
	 * The next field will be LIST_POISON1 if this RPC hasn't yet been
	 * linked into @hsk->active_rpcs. Access with RCU.
	 */
	struct list_head active_links;
	
	/** @dead_links: For linking this object into @hsk->dead_rpcs. */
	struct list_head dead_links;
	
	/**
	 * @interest: Describes a thread that wants to be notified when
	 * msgin is complete, or NULL if none.
	 */
	struct homa_interest *interest;
	
	/**
	 * @ready_links: Used to link this object into
	 * &homa_sock.ready_requests or &homa_sock.ready_responses.
	 */
	struct list_head ready_links;
	
	/**
	 * @grantable_links: Used to link this RPC into homa->grantable_rpcs.
	 * If this RPC isn't in homa->grantable_rpcs, this is an empty
	 * list pointing to itself.
	 */
	struct list_head grantable_links;
	
	/**
	 * @throttled_links: Used to link this RPC into homa->throttled_rpcs.
	 * If this RPC isn't in homa->throttled_rpcs, this is an empty
	 * list pointing to itself.
	 */
	struct list_head throttled_links;
	
	/**
	 * DCACP logic: Used to link this RPC into peer->incoming_rpcs if 
	 * current state is HOMA_INCOMING;
	 * If this RPC isn't in homa->throttled_rpcs, this is an empty
	 * list pointing to itself.
	 */
	struct list_head peer_links;

	/**
	 * @silent_ticks: Number of times homa_timer has been invoked
	 * since the last time a packet was received for this RPC.
	 */
	int silent_ticks;
	
	/**
	 * @num_resends: the number of RESEND requests we have sent since
	 * the last time we received a packet for this RPC from @peer.
	 */
	int num_resends;
};

/**
 * homa_rpc_lock() - Acquire the lock for an RPC.
 * @rpc:   RPC to lock. Note: this function is only safe under
 *         limited conditions. The caller must ensure that the RPC
 *         cannot be reaped before the lock is acquired. It cannot
 *         do that by acquiring the socket lock, since that violates
 *         lock ordering constraints. One approach is to increment
 *         rpc->hsk->reap_disable. Don't use this function unless you
 *         are very sure what you are doing!  See sync.txt for more
 *         info on locking.
 */
inline static void homa_rpc_lock(struct homa_rpc *rpc) {
	if (!spin_trylock_bh(rpc->lock))
		homa_rpc_lock_slow(rpc);
}

/**
 * homa_rpc_unlock() - Release the lock for an RPC.
 * @rpc:   RPC to unlock.
 */
inline static void homa_rpc_unlock(struct homa_rpc *rpc) {
	spin_unlock_bh(rpc->lock);
}

/**
 * define HOMA_SOCKTAB_BUCKETS - Number of hash buckets in a homa_socktab.
 * Must be a power of 2.
 */
#define HOMA_SOCKTAB_BUCKETS 1024

/**
 * struct homa_socktab - A hash table that maps from port numbers (either
 * client or server) to homa_sock objects.
 *
 * This table is managed exclusively by homa_socktab.c, using RCU to
 * minimize synchronization during lookups.
 */
struct homa_socktab {
	/**
	 * @mutex: Controls all modifications to this object; not needed
	 * for socket lookups (RCU is used instead). Also used to
	 * synchronize port allocation.
	 */
	struct mutex write_lock;
	
	/**
	 * @buckets: Heads of chains for hash table buckets. Chains
	 * consist of homa_socktab_link objects.
	 */
	struct hlist_head buckets[HOMA_SOCKTAB_BUCKETS];
};

/**
 * struct homa_socktab_links - Used to link homa_socks into the hash chains
 * of a homa_socktab.
 */
struct homa_socktab_links {
	/* Must be the first element of the struct! */
	struct hlist_node hash_links;
	struct homa_sock *sock;
};

/**
 * struct homa_socktab_scan - Records the state of an iteration over all
 * the entries in a homa_socktab, in a way that permits RCU-safe deletion
 * of entries.
 */
struct homa_socktab_scan {
	/** @socktab: The table that is being scanned. */
	struct homa_socktab *socktab;
	
	/**
	 * @current_bucket: the index of the bucket in socktab->buckets
	 * currently being scanned. If >= HOMA_SOCKTAB_BUCKETS, the scan
	 * is complete.
	 */
	int current_bucket;
	
	/**
	 * @next: the next socket to return from homa_socktab_next (this
	 * socket has not yet been returned). NULL means there are no
	 * more sockets in the current bucket.
	 */
	struct homa_socktab_links *next;
};

/**
 * define HOMA_CLIENT_RPC_BUCKETS - Number of buckets in hash tables for
 * client RPCs. Must be a power of 2.
 */
#define HOMA_CLIENT_RPC_BUCKETS 1024

/**
 * define HOMA_SERVER_RPC_BUCKETS - Number of buckets in hash tables for
 * server RPCs. Must be a power of 2.
 */
#define HOMA_SERVER_RPC_BUCKETS 1024

struct homa_rpc_bucket {
	/**
	 * @lock: serves as a lock both for this bucket (e.g., when
	 * adding and removing RPCs) and also for all of the RPCs in
	 * the bucket. Must be held whenever manipulating an RPC in
	 * this bucket. This dual purpose permits clean and safe
	 * deletion and garbage collection of RPCs.
	 */
	struct spinlock lock;
	
	/** @rpcs: list of RPCs that hash to this bucket. */
	struct hlist_head rpcs;
};

/**
 * struct homa_sock - Information about an open socket.
 */
struct homa_sock {
	/** @inet: Generic socket data; must be the first field. */
	struct inet_sock inet;
	
	/**
	 * @lock: Must be held when modifying fields such as interests
	 * and lists of RPCs. This lock is used in place of sk->sk_lock
	 * because it's used differently (it's always used as a simple
	 * spin lock).  See sync.txt for more on Homa's synchronization
	 * strategy.
	 */
	struct spinlock lock;
	
	/**
	 * @last_locker: identifies the code that most recently acquired
	 * @lock successfully. Occasionally used for debugging. */
	char *last_locker;
	
	/**
	 * @reap_disable: no RPCs for this socket will be reaped while
	 * this counter is nonzero. Allows safe traversal of lists
	 * without holding socket lock. See sync.txt for more info.
	 */
	atomic_t reap_disable;
	
	/**
	 * @homa: Overall state about the Homa implementation. NULL
	 * means this socket has been deleted.
	 */
	struct homa *homa;
	
	/** @shutdown: True means the socket is no longer usable. */
	bool shutdown;
	
	/**
	 * @server_port: Port number for receiving incoming RPC requests.
	 * Must be assigned explicitly with bind; 0 means not bound yet.
	 */
	__u16 server_port;
	
	/** @client_port: Port number to use for outgoing RPC requests. */
	__u16 client_port;
	
	/** @next_outgoing_id: Id to use for next outgoing RPC request. Can
	 * be incremented using atomic ops to assign the next id without
	 * acquiring @lock.
	 */
	atomic64_t next_outgoing_id;
	
	/**
	 * @client_socktab_links: Links this socket into the homa_socktab
	 * based on client_port.
	 */
	struct homa_socktab_links client_links; 
	
	/**
	 * @client_socktab_links: Links this socket into the homa_socktab
	 * based on server_port. Invalid/unused if server_port is 0.
	 */
	struct homa_socktab_links server_links;
	
	/** @active_rpcs: List of all existing RPCs related to this socket,
	 * including both client and server RPCs. This list isn't strictly
	 * needed, since RPCs are already in one of the hash tables below,
	 * but it's more efficient for homa_timer to have this list
	 * (so it doesn't have to scan large numbers of hash buckets).
	 * The list is sorted, with the oldest RPC first. Manipulate with
	 * RCU so timer can access without locking.
	 */
	struct list_head active_rpcs;
	
	/**
	 * @dead_rpcs: Contains RPCs for which homa_rpc_free has been
	 * called, but their packet buffers haven't yet been freed.
	 */
	struct list_head dead_rpcs;
	
	/** @dead_skbs: Total number of socket buffers in RPCS on dead_rpcs. */
	int dead_skbs;
	
	/**
	 * @ready_requests: Contains server RPCs in RPC_READY state that
	 * have not yet been claimed. The head is oldest, i.e. next to return.
	 */
	struct list_head ready_requests;
	
	/**
	 * @ready_responses: Contains client RPCs in RPC_READY state that
	 * have not yet been claimed. The head is oldest, i.e. next to return.
	 */
	struct list_head ready_responses;
	
	/**
	 * @request_interests: List of threads that want to receive incoming
	 * request messages.
	 */
	struct list_head request_interests;
	
	/**
	 * @response_interests: List of threads that want to receive incoming
	 * response messages.
	 */
	struct list_head response_interests;
	
	/**
	 * @client_rpc_buckets: Hash table for fast lookup of client RPCs.
	 * Modifications are synchronized with bucket locks, not
	 * the socket lock.
	 */
	struct homa_rpc_bucket client_rpc_buckets[HOMA_CLIENT_RPC_BUCKETS];
	
	/**
	 * @server_rpc_buckets: Hash table for fast lookup of server RPCs.
	 * Modifications are synchronized with bucket locks, not
	 * the socket lock.
	 */
	struct homa_rpc_bucket server_rpc_buckets[HOMA_SERVER_RPC_BUCKETS];
};

/**
 * define HOMA_PEERTAB_BUCKETS - Number of bits in the bucket index for a
 * homa_peertab.  Should be large enough to hold an entry for every server
 * in a datacenter without long hash chains.
 */
#define HOMA_PEERTAB_BUCKET_BITS 20

/** define HOME_PEERTAB_BUCKETS - Number of buckets in a homa_peertab. */
#define HOMA_PEERTAB_BUCKETS (1 << HOMA_PEERTAB_BUCKET_BITS)

/**
 * struct homa_peertab - A hash table that maps from IPV4 addresses
 * to homa_peer objects. Entries are gradually added to this table,
 * but they are never removed except when the entire table is deleted.
 * We can't safely delete because results returned by homa_peer_find
 * may be retained indefinitely.
 *
 * This table is managed exclusively by homa_peertab.c, using RCU to
 * permit efficient lookups.
 */
struct homa_peertab {
	/**
	 * @write_lock: Synchronizes addition of new entries; not needed
	 * for lookups (RCU is used instead).
	 */
	struct spinlock write_lock;
	
	/**
	 * @buckets: Pointer to heads of chains of homa_peers for each bucket.
	 * Malloc-ed, and must eventually be freed. NULL means this structure
	 * has not been initialized.
	 */
	struct hlist_head *buckets;
};

/**
 * struct homa_peer - One of these objects exists for each machine that we
 * have communicated with (either as client or server). 
 */
struct homa_peer {
	/** @daddr: IPV4 address for the machine. */
	__be32 addr;
	
	/** @flow: Addressing info needed to send packets. */
	struct flowi flow;
	
	/**
	 * @dst: Used to route packets to this peer; we own a reference
	 * to this, which we must eventually release.
	 */
	struct dst_entry *dst;
	
	/**
	 * @unsched_cutoffs: priorities to use for unscheduled packets
	 * sent to this host, as specified in the most recent CUTOFFS
	 * packet from that host. See documentation for @homa.unsched_cutoffs
	 * for the meanings of these values.
	 */
	int unsched_cutoffs[HOMA_MAX_PRIORITIES];
	
	/**
	 * @cutoff_version: value of cutoff_version in the most recent
	 * CUTOFFS packet received from this peer.  0 means we haven't
	 * yet received a CUTOFFS packet from the host. Note that this is
	 * stored in network byte order.
	 */
	__be16 cutoff_version;
	
	/**
	 * last_update_jiffies: time in jiffies when we sent the most
	 * recent CUTOFFS packet to this peer.
	 */
	unsigned long last_update_jiffies;
	
	/**
	 * last_resend_tick: value of @homa->timer_ticks when the most recent
	 * RESEND request was sent to this peer. Manipulated only by
	 * homa_timer, so no synchronization needed.
	 */
	__u32 last_resend_tick;
	
	/**
	* TODO: DCACP homa_message_in priority queue based on remaining flow size;
	*/
	struct list_head incoming_rpcs;


	/**
	 * @peertab_links: Links this object into a bucket of its
	 * homa_peertab.
	 */
	struct hlist_node peertab_links;
};

#define MAX_DCACP_INCAST_RATIO 50
/* 
 dcacp rts
*/
struct dcacp_rts {
    uint8_t iter;
    uint32_t src_addr;
    __be64 remaining_sz;
};

/*
dcacp grant
*/
struct dcacp_grant {
    bool prompt;
    uint32_t dst_addr;
    __be64 remaining_sz;
};



/* dcacp epoch

*/
struct dcacp_epoch {
	/**
	 * @lock: Must be held when modifying fields. This lock is used in place of sk->sk_lock
	 * because it's used differently (it's always used as a simple
	 * spin lock).  See sync.txt for more on Homa's synchronization
	 * strategy.
	 */
	struct spinlock lock;
	int epoch;
	int iter;
	bool prompt;
	uint32_t match_src_addr;
	uint32_t match_dst_addr;
	struct dcacp_grant grants_q[MAX_DCACP_INCAST_RATIO];
	struct dcacp_rts  rts_q[MAX_DCACP_INCAST_RATIO];
	struct dcacp_rts* min_rts;
	struct dcacp_grant* min_grant;
	int grant_size;
	int rts_size;
	// struct rte_timer epoch_timer;
	// struct rte_timer sender_iter_timers[10];
	// struct rte_timer receiver_iter_timers[10];
	// struct pim_timer_params pim_timer_params;
	// uint64_t start_cycle;
};
/**
 * struct homa - Overall information about the Homa protocol implementation.
 * 
 * There will typically only exist one of these at a time, except during
 * unit tests.
 */
struct homa {
	/**
	 * @next_client_port: A client port number to consider for the
	 * next Homa socket; increments monotonically. Current value may
	 * be in the range allocated for servers; must check before using.
	 * This port may also be in use already; must check.
	 */
	__u16 next_client_port;
	
	/**
	 * @port_map: Information about all open sockets; indexed by
	 * port number.
	 */
	struct homa_socktab port_map;
	
	/**
	 * @peertab: Info about all the other hosts we have communicated
	 * with; indexed by host IPV4 address.
	 */
	struct homa_peertab peers;
	
	/**
	 * DCACP Epoch
	 */
	struct dcacp_epoch epoch;

	/**
	 * TODO: add epoch thread to handle request 
	 */
	/**
	 * @rtt_bytes: A conservative estimate of the amount of data that
	 * can be sent over the wire in the time it takes to send a full-size
	 * data packet and receive back a grant. Homa tries to ensure
	 * that there is at least this much data in transit (or authorized
	 * via grants) for an incoming message at all times.  Set externally
	 * via sysctl, but Homa will always round up to an even number of
	 * full-size packets.
	 */
	int rtt_bytes;
	
	/**
	 * @link_bandwidth: The raw bandwidth of the network uplink, in
	 * units of 1e06 bits per second.  Set externally via sysctl.
	 */
	int link_mbps;
	
	/**
	 * @num_priorities: The total number of priority levels available for
	 * Homa's use. Internally, Homa will use priorities from 0 to
	 * num_priorities-1, inclusive. Set externally via sysctl.
	 */
	int num_priorities;
	
	/**
	 * @base_priority: when specifying priorities in outbound packets,
	 * Homa will add this value to each internally computed priority;
	 * these values are then mapped to VLAN QOS levels by Linux (see
	 * vconfig(1)).
	 * Set externally via sysctl.
	 */
	int base_priority;
	
	/**
	 * @max_sched_prio: The highest priority level currently available for
	 * scheduled packets. Levels above this are reserved for unscheduled
	 * packets.  Set externally via sysctl.
	 */
	int max_sched_prio;
	
	/**
	 * @unsched_cutoffs: the current priority assignments for incoming
	 * unscheduled packets. The value of entry i is the largest
	 * message size that uses priority i (larger i is higher priority).
	 * If entry i has a value of HOMA_MAX_MESSAGE_SIZE or greater, then
	 * priority levels less than i will not be used for unscheduled
	 * packets. At least one entry in the array must have a value of
	 * HOMA_MAX_MESSAGE_SIZE or greater (entry 0 is usually INT_MAX).
	 *Set externally via sysctl.
	 */
	int unsched_cutoffs[HOMA_MAX_PRIORITIES];
	
	/**
	 * @cutoff_version: increments every time unsched_cutoffs is
	 * modified. Used to determine when we need to send updates to
	 * peers.  Note: 16 bits should be fine for this: the worst
	 * that happens is a peer has a super-stale value that equals
	 * our current value, so the peer uses suboptimal cutoffs until the
	 * next version change.  Can be set externally via sysctl.
	 */
	int cutoff_version;
	
	/**
	 * @grant_increment: each grant sent by a Homa receiver will allow
	 * this many additional bytes to be sent by the receiver.
	 */
	int grant_increment;
	
	/**
	 * @max_overcommit: The maximum number of messages to which Homa will
	 * send grants at any given point in time.  Set externally via sysctl.
	 */
	int max_overcommit;
	
	/**
	 * @resend_ticks: When an RPC's @silent_ticks reaches this value,
	 * start sending RESEND requests.
	 */
	int resend_ticks;
	
	/**
	 * @resend_interval: minimum number of homa timer ticks between
	 * RESENDs to the same peer.
	 */
	int resend_interval;
	
	/**
	 * @abort_resends: Abort an RPC if there is still no response
	 * after this many resends.
	 */
	int abort_resends;
	
	/**
	 * @reap_limit: Maximum number of packet buffers to free in a
	 * single call to home_rpc_reap.
	 */
	int reap_limit;
	
	/**
	 * @max_dead_buffs: If the number of packet buffers in dead but
	 * not yet reaped RPCs is less than this number, then Homa reaps
	 * RPCs in a way that minimizes impact on performance but may permit
	 * dead RPCs to accumulate. If the number of dead packet buffers
	 * exceeds this value, then Homa switches to a more aggressive approach
	 * to reaping RPCs, which is more likely to impact performance.
	 */
	int max_dead_buffs;
	
	/**
	 * @grantable_lock: Used to synchronize access to @grantable_rpcs and
	 * @num_grantable.
	 */
	struct spinlock grantable_lock;
	
	/**
	 * @grantable_rpcs: Contains all homa_rpcs (both requests and
	 * responses) whose msgins require additional grants before they can
	 * complete. The list is sorted in priority order (head has fewest
	 * bytes_remaining).
	 */
	struct list_head grantable_rpcs;
	
	/** @num_grantable: The number of messages in grantable_msgs. */
	int num_grantable;
	
	/**
	 * @throttle_lock: Used to synchronize access to @throttled_rpcs. To
	 * insert or remove an RPC from throttled_rpcs, must first acquire
	 * the RPC's socket lock, then this lock.
	 */
	struct spinlock throttle_lock;
	
	/**
	 * @throttled_rpcs: Contains all homa_rpcs that have bytes ready
	 * for transmission, but which couldn't be sent without exceeding
	 * the queue limits for transmission. Manipulate only with "_rcu"
	 * functions.
	 */
	struct list_head throttled_rpcs;
	
	/**
	 * @throttle_min_bytes: If a packet has fewer bytes than this, then it
	 * bypasses the throttle mechanism and is transmitted immediately.
	 * We have this limit because for very small packets we can't keep
	 * up with the NIC (we're limited by CPU overheads); there's no
	 * need for throttling and going through the throttle mechanism
	 * adds overhead, which slows things down. At least, that's the
	 * hypothesis (needs to be verified experimentally!). Set externally
	 * via sysctl.
	 */
	int throttle_min_bytes;
	
	/**
	 * @pacer_kthread: Kernel thread that transmits packets from
	 * throttled_rpcs in a way that limits queue buildup in the
	 * NIC.
	 */
	struct task_struct *pacer_kthread;
	
	/**
	 * @pacer_exit: true means that the pacer thread should exit as
	 * soon as possible.
	 */
	bool pacer_exit;
	
	/**
	 * @pacer_active: synchronization variable: 1 means an instance
	 * of homa_pacer_xmit is already running, 0 means not.
	 */
	atomic_t pacer_active;
	
	/**
	 * @link_idle_time: The time, measured by get_cycles() at which we
	 * estimate that all of the packets we have passed to Linux for
	 * transmission will have been transmitted. May be in the past.
	 * This estimate assumes that only Homa is transmitting data, so
	 * it could be a severe underestimate if there is competing traffic 
	 * from, say, TCP. Access only with atomic ops.
	 */
	atomic64_t link_idle_time;
	
	/**
	 * @max_nic_queue_ns: Limits the NIC queue length: we won't queue
	 * up a packet for transmission if link_idle_time is this many
	 * nanoseconds in the future (or more). Set externally via sysctl.
	 */
	int max_nic_queue_ns;
	
	/**
	 * @max_nic_queue_cycles: Same as max_nic_queue_ns, except in units
	 * of get_cycles().
	 */
	int max_nic_queue_cycles;
	
	/**
	 * @cycles_per_kbyte: the number of cycles, as measured by get_cycles(),
	 * that it takes to transmit 1000 bytes on our uplink. This is actually
	 * a slight overestimate of the value, to ensure that we don't
	 * underestimate NIC queue length and queue too many packets.
	 */
	__u32 cycles_per_kbyte;
	
	/**
	 * @verbose: Nonzero enables additional logging. Set externally via
	 * sysctl.
	 */
	int verbose;
	
	/**
	 * @max_gso_size: Maximum number of bytes that will be included
	 * in a single output packet. Can be set externally via sysctl to
	 * lower the limit already enforced by Linux.
	 */
	int max_gso_size;
	
	/**
	 * @max_gro_skbs: Maximum number of socket buffers that can be
	 * aggregated by the GRO mechanism.
	 */
	int max_gro_skbs;
	
	/**
	 * @timer_ticks: number of times that homa_timer has been invoked
	 * (may wraparound, which is safe).
	 */
	uint32_t timer_ticks;
	
	/**
	 * @metrics_lock: Used to synchronize accesses to @metrics_active_opens
	 * and updates to @metrics.
	 */
	struct spinlock metrics_lock;
	
	/*
	 * @metrics: a human-readable string containing recent values
	 * for all the Homa performance metrics, as generated by
	 * homa_compile_metrics. This string is kmalloc-ed; NULL means
	 * homa_compile_metrics has never been called.
	 */
	char* metrics;
	
	/** @metrics_capacity: number of bytes available at metrics. */
	size_t metrics_capacity;
	
	/**
	 * @metrics_length: current length of the string in metrics,
	 * not including terminating NULL character.
	 */
	size_t metrics_length;
	
	/**
	 * @metrics_active_opens: number of open struct files that
	 * currently exist for the metrics file in /proc.
	 */
	int metrics_active_opens;
	
	/**
	 * @flags: a collection of bits that can be set using sysctl
	 * to trigger various behaviors.
	 */
	int flags;
	
	/**
	 * @temp: the values in this array can be read and written with sysctl.
	 * They have no officially defined purpose, and are available for
	 * short-term use during testing.
	 */
	int temp[4];
};

/**
 * struct homa_metrics - various performance counters kept by Homa.
 *
 * There is one of these structures for each core, so counters can
 * be updated without worrying about synchronization or extra cache
 * misses. This isn't quite perfect (it's conceivable that a process
 * could move from one CPU to another in the middle of updating a counter),
 * but this is extremely unlikely, and we can tolerate the occasional
 * miscounts that might result.
 * 
 * All counters are free-running: they never reset.
 */
#define HOMA_NUM_SMALL_COUNTS 64
#define HOMA_NUM_MEDIUM_COUNTS 64
struct homa_metrics {	
	/**
	 * @small_msg_counts: entry i holds the total number of bytes
	 * received in messages whose length is between 64*i and 64*i + 63,
	 * inclusive.
	 */
	__u64 small_msg_bytes[HOMA_NUM_SMALL_COUNTS];
	
	/**
	 * @medium_msg_counts: entry i holds the total number of bytes
	 * received in messages whose length is between 1024*i and
	 * 1024*i + 1023, inclusive. The first four entries are always 0
	 * (small_msg_counts covers this range).
	 */
	__u64 medium_msg_bytes[HOMA_NUM_MEDIUM_COUNTS];
	
	/**
	 * @large_msg_count: the total number of messages received whose
	 * length is 0x100 or greater.
	 */
	__u64 large_msg_bytes;
	
	/**
	 * @packets_sent: total number of packets sent for each packet type
	 * (entry 0 corresponds to DATA, and so on).
	 */
	__u64 packets_sent[BOGUS-DATA];
	
	/**
	 * @packets_received: total number of packets received for each
	 * packet type (entry 0 corresponds to DATA, and so on).
	 */
	__u64 packets_received[BOGUS-DATA];
	
	/**
	 * @requests_received: total number of request messages received.
	 */
	__u64 requests_received;
	
	/**
	 * @responses_received: total number of response messages received.
	 */
	__u64 responses_received;
	
	/**
	 * @pkt_recv_calls: total number of calls to homa_pkt_recv (i.e.,
	 * total number of GRO packets processed, each of which could contain
	 * multiple Homa packets.
	 */
	__u64 pkt_recv_calls;
	
	/**
	 * @timer_cycles: total time spent in homa_timer, as measured with
	 * get_cycles().
	 */
	__u64 timer_cycles;

	/**
	 * @pacer_cycles: total time spent executing in homa_pacer_main
	 * (not including blocked time), as measured with get_cycles().
	 */
	__u64 pacer_cycles;
	
	/**
	 * @pacer_lost_cycles: unnecessary delays in transmitting packets
	 * (i.e. wasted output bandwidth) because the pacer was slow or got
	 * descheduled.
	 */
	__u64 pacer_lost_cycles;
	
	/**
	 * @pacer_skipped_rpcs: total number of times that the pacer had to
	 * abort because it couldn't lock an RPC.
	 */
	__u64 pacer_skipped_rpcs;

	/**
	 * @resent_packets: total number of data packets issued in response to
	 * RESEND packets.
	 */
	__u64 resent_packets;
	
	/**
	 * @peer_hash_links: total # of link traversals in homa_peer_find.
	 */
	__u64 peer_hash_links;
	
	/**
	 * @peer_new_entries: total # of new entries created in Homa's
	 * peer table (this value doesn't increment if the desired peer is
	 * found in the entry in its hash chain).
	 */
	__u64 peer_new_entries;
	
	/**
	 * @peer_kmalloc errors: total number of times homa_peer_find
	 * returned an error because it couldn't allocate memory for a new
	 * peer.
	 */
	__u64 peer_kmalloc_errors;
	
	/**
	 * @peer_route errors: total number of times homa_peer_find
	 * returned an error because it couldn't create a route to the peer.
	 */
	__u64 peer_route_errors;
	
	/**
	 * @control_xmit_errors errors: total number of times ip_queue_xmit
	 * failed when transmitting a control packet.
	 */
	__u64 control_xmit_errors;
	
	/**
	 * @data_xmit_errors errors: total number of times ip_queue_xmit
	 * failed when transmitting a data packet.
	 */
	__u64 data_xmit_errors;
	
	/**
	 * @unknown_rpc: total number of times an incoming packet was
	 * discarded because it referred to a nonexistent RPC.
	 */
	__u64 unknown_rpcs;
	
	/**
	 * @cant_create_server_rpc: total number of times a server discarded
	 * an incoming packet because it couldn't create a homa_rpc object.
	 */
	__u64 server_cant_create_rpcs;
	
	/**
	 * @unknown_packet_type: total number of times a packet was discarded
	 * because its type wasn't one of the supported values.
	 */
	__u64 unknown_packet_types;
	
	/**
	 * @short_packets: total number of times a packet was discarded
	 * because it was too short to hold all the required information.
	 */
	__u64 short_packets;

	/**
	 * @client_rpc_timeouts: total number of times an RPC was aborted on
	 * the client side because of a timeout.
	 */
	
	__u64 client_rpc_timeouts;

	/**
	 * @server_rpc_timeouts: total number of times an RPC was aborted on
	 * the server side because of a timeout.
	 */
	
	__u64 server_rpc_timeouts;

	/**
	 * @client_lock_misses: total number of times that Homa had to wait
	 * to acquire a client bucket lock.
	 */
	__u64 client_lock_misses;

	/**
	 * @client_lock_miss_cycles: total time spent waiting for client
	 * bucket lock misses, measured by get_cycles().
	 */
	__u64 client_lock_miss_cycles;

	/**
	 * @server_lock_misses: total number of times that Homa had to wait
	 * to acquire a server bucket lock.
	 */
	__u64 server_lock_misses;

	/**
	 * @server_lock_miss_cycles: total time spent waiting for server
	 * bucket lock misses, measured by get_cycles().
	 */
	__u64 server_lock_miss_cycles;

	/**
	 * @socket_lock_miss_cycles: total time spent waiting for socket
	 * lock misses, measured by get_cycles().
	 */
	__u64 socket_lock_miss_cycles;

	/**
	 * @socket_lock_misses: total number of times that Homa had to wait
	 * to acquire a socket lock.
	 */
	__u64 socket_lock_misses;

	/**
	 * @throttle_lock_miss_cycles: total time spent waiting for throttle
	 * lock misses, measured by get_cycles().
	 */
	__u64 throttle_lock_miss_cycles;

	/**
	 * @throttle_lock_misses: total number of times that Homa had to wait
	 * to acquire the throttle lock.
	 */
	__u64 throttle_lock_misses;

	/**
	 * @grantable_lock_miss_cycles: total time spent waiting for grantable
	 * lock misses, measured by get_cycles().
	 */
	__u64 grantable_lock_miss_cycles;

	/**
	 * @grantable_lock_misses: total number of times that Homa had to wait
	 * to acquire the grantable lock.
	 */
	__u64 grantable_lock_misses;

	/**
	 * @disabled_reaps: total number of times that the reaper couldn't
	 * run at all because it was disabled.
	 */
	__u64 disabled_reaps;

	/**
	 * @disabled_rpc_reaps: total number of times that the reaper skipped
	 * an RPC because reaping was disabled for that particular RPC
	 */
	__u64 disabled_rpc_reaps;
	
	/**
	 * @reaper_runs: total number of times that the reaper was invoked
	 * and was not disabled.
	 */
	__u64 reaper_calls;
	
	/**
	 * @reaper_dead_skbs: incremented by hsk->dead_skbs each time that
	 * reaper_calls is incremented.
	 */
	__u64 reaper_dead_skbs;
	
	/**
	 * @temp1: this value, and the others below it, are reserved for
	 * temporary use during testing.
	 */
	__u64 temp1;
	__u64 temp2;
	__u64 temp3;
	__u64 temp4;
};

#define homa_bucket_lock(bucket, type)                      \
	if (unlikely(!spin_trylock_bh(&bucket->lock))) {    \
		__u64 start = get_cycles();                 \
		INC_METRIC(type##_lock_misses, 1);        \
		spin_lock_bh(&bucket->lock);                \
		INC_METRIC(type##_lock_miss_cycles, get_cycles() - start); \
	}

/**
 * homa_client_rpc_bucket() - Find the bucket containing a given
 * client RPC.
 * @hsk:      Socket associated with the RPC.
 * @id:       Id of the desired RPC.
 * 
 * Return:    The bucket in which this RPC will appear, if the RPC exists.
 */
static inline struct homa_rpc_bucket *homa_client_rpc_bucket(
		struct homa_sock *hsk, __u64 id)
{
	/* We can use a really simple hash function here because RPC ids
	 * are allocated sequentially.
	 */
	return &hsk->client_rpc_buckets[id & (HOMA_CLIENT_RPC_BUCKETS - 1)];
}

/**
 * homa_data_offset() - Returns the offset-within-message of the first
 * byte in a data packet.
 * @skb:  Must contain a valid data packet.
 */
static inline int homa_data_offset(struct sk_buff *skb)
{
	return ntohl(((struct data_header *) skb_transport_header(skb))
			->seg.offset);
}

/*
 * homa_interest_set() - Assign a particular RPC to a particular interest;
 * this synchronizes with a thread waiting for the RPC.
 * @interest:  Interest to fill in.
 * @rpc:       RPC to assign to @interest.
 */
inline static void homa_interest_set(struct homa_interest *interest,
		struct homa_rpc *rpc)
{
	interest->peer_addr = rpc->peer->addr;
	interest->peer_port = rpc->dport;
	interest->is_client = rpc->is_client;
	
	/* Must set last for proper synchronization. */
	atomic_long_set_release(&interest->id, rpc->id);
}

/**
 * homa_next_skb() - Compute address of Homa's private link field in @skb.
 * @skb:     Socket buffer containing private link field.
 * 
 * Homa needs to keep a list of buffers in a message, but it can't use the
 * links built into sk_buffs because Homa wants to retain its list even
 * after sending the packet, and the built-in links get used during sending.
 * Thus we allocate extra space at the very end of the packet's data
 * area to hold a forward pointer for a list.
 */
static inline struct sk_buff **homa_next_skb(struct sk_buff *skb)
{
	return (struct sk_buff **) (skb_end_pointer(skb) - sizeof(char*));
}

/**
 * port_hash() - Hash function for port numbers.
 * @port:   Port number being looked up.
 *
 * Return:  The index of the bucket in which this port will be found (if
 *          it exists.
 */
static inline int homa_port_hash(__u16 port)
{
	/* We can use a really simple hash function here because client
	 * port numbers are allocated sequentially and server port numbers
	 * are unpredictable.
	 */
	return port & (HOMA_SOCKTAB_BUCKETS - 1);
}

/**
 * homa_server_rpc_bucket() - Find the bucket containing a given
 * server RPC.
 * @hsk:         Socket associated with the RPC.
 * @id:          Id of the desired RPC.
 * 
 * Return:    The bucket in which this RPC will appear, if the RPC exists.
 */
static inline struct homa_rpc_bucket *homa_server_rpc_bucket(
		struct homa_sock *hsk, __u64 id)
{
	/* Each client allocates RPC ids sequentially, so they will
	 * naturally distribute themselves across the hash space.
	 * Thus we can use the id directly as hash.
	 */
	return &hsk->server_rpc_buckets[id & (HOMA_SERVER_RPC_BUCKETS - 1)];
}

/**
 * homa_set_doff() - Fills in the doff TCP header field for a Homa packet.
 * @h:   Packet header whose doff field is to be set.
 */
static inline void homa_set_doff(struct data_header *h)
{
	h->common.doff = (sizeof(struct data_header)
			- sizeof(struct data_segment)) << 2;
}

static inline struct homa_sock *homa_sk(const struct sock *sk)
{
	return (struct homa_sock *)sk;
}

/**
 * homa_sock_lock() - Acquire the lock for a socket. If the socket
 * isn't immediately available, record stats on the waiting time.
 * @hsk:     Socket to lock.
 * @locker:  Static string identifying where the socket was locked;
 *           used to track down deadlocks.
 */
static inline void homa_sock_lock(struct homa_sock *hsk, char *locker) {
	if (!spin_trylock_bh(&hsk->lock)) {
//		printk(KERN_NOTICE "Slow path for socket %d, last locker %s",
//				hsk->client_port, hsk->last_locker);
		homa_sock_lock_slow(hsk);
	}
//	hsk->last_locker = locker;
}

/**
 * homa_sock_unlock() - Release the lock for a socket.
 * @hsk:   Socket to lock.
 */
static inline void homa_sock_unlock(struct homa_sock *hsk) {
	spin_unlock_bh(&hsk->lock);
}

/**
 * homa_grantable_lock() - Acquire the grantable lock. If the lock
 * isn't immediately available, record stats on the waiting time.
 * @homa:    Overall data about the Homa protocol implementation.
 */
static inline void homa_grantable_lock(struct homa *homa) {
	if (!spin_trylock_bh(&homa->grantable_lock)) {
		homa_grantable_lock_slow(homa);
	}
}

/**
 * homa_grantable_unlock() - Release the grantable lock.
 * @homa:    Overall data about the Homa protocol implementation.
 */
static inline void homa_grantable_unlock(struct homa *homa) {
	spin_unlock_bh(&homa->grantable_lock);
}

/**
 * homa_throttle_lock() - Acquire the throttle lock. If the lock
 * isn't immediately available, record stats on the waiting time.
 * @homa:    Overall data about the Homa protocol implementation.
 */
static inline void homa_throttle_lock(struct homa *homa) {
	if (!spin_trylock_bh(&homa->throttle_lock)) {
		homa_throttle_lock_slow(homa);
	}
}

/**
 * homa_throttle_unlock() - Release the throttle lock.
 * @homa:    Overall data about the Homa protocol implementation.
 */
static inline void homa_throttle_unlock(struct homa *homa) {
	spin_unlock_bh(&homa->throttle_lock);
}

#define INC_METRIC(metric, count) \
		(homa_metrics[smp_processor_id()]->metric) += (count)

extern struct homa_metrics *homa_metrics[NR_CPUS];

#ifdef __UNIT_TEST__
extern void unit_log_printf(const char *separator, const char* format, ...)
		__attribute__((format(printf, 2, 3)));
#define UNIT_LOG unit_log_printf
#else
#define UNIT_LOG(...)
#endif

extern void     homa_add_packet(struct homa_message_in *msgin,
			struct sk_buff *skb);
extern void     homa_add_to_throttled(struct homa_rpc *rpc);
extern void     homa_append_metric(struct homa *homa, const char* format, ...);
extern int      homa_backlog_rcv(struct sock *sk, struct sk_buff *skb);
extern int      homa_bind(struct socket *sk, struct sockaddr *addr, int addr_len);
extern int      homa_check_timeout(struct homa_rpc *rpc);
extern void     homa_prios_changed(struct homa *homa);
extern int      homa_check_nic_queue(struct homa *homa, struct sk_buff *skb,
			bool force);
extern void     homa_close(struct sock *sock, long timeout);
extern void     homa_compile_metrics(struct homa_metrics *m);
extern void     homa_cutoffs_pkt(struct sk_buff *skb, struct homa_sock *hsk);
extern void     homa_data_from_server(struct sk_buff *skb,
			struct homa_rpc *crpc);
extern int      homa_data_pkt(struct sk_buff *skb, struct homa_rpc *rpc);
extern void     homa_destroy(struct homa *homa);
extern int      homa_diag_destroy(struct sock *sk, int err);
extern int      homa_disconnect(struct sock *sk, int flags);
extern int      homa_dointvec(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern int     homa_err_handler(struct sk_buff *skb, u32 info);
extern struct sk_buff
                *homa_fill_packets(struct homa *homa, struct homa_peer *peer,
			char __user *from, size_t len);
extern struct homa_rpc
               *homa_find_client_rpc(struct homa_sock *hsk, __u64 id);
extern struct homa_rpc
	       *homa_find_server_rpc(struct homa_sock *hsk, __be32 saddr,
			__u16 sport, __u64 id);
extern void     homa_free_skbs(struct sk_buff *skb);
extern int      homa_get_port(struct sock *sk, unsigned short snum);
extern void     homa_get_resend_range(struct homa_message_in *msgin,
			struct resend_header *resend);
extern int      homa_getsockopt(struct sock *sk, int level, int optname,
			char __user *optval, int __user *option);
extern void     homa_grant_pkt(struct sk_buff *skb, struct homa_rpc *rpc);
extern int      homa_gro_complete(struct sk_buff *skb, int thoff);
extern struct sk_buff
		*homa_gro_receive(struct list_head *head, struct sk_buff *skb);
extern int      homa_hash(struct sock *sk);
extern enum hrtimer_restart
		homa_hrtimer(struct hrtimer *timer);
extern int      homa_init(struct homa *homa);
extern int      homa_ioc_recv(struct sock *sk, unsigned long arg);
extern int      homa_ioc_reply(struct sock *sk, unsigned long arg);
extern int      homa_ioc_send(struct sock *sk, unsigned long arg);
extern int      homa_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern void     homa_manage_grants(struct homa *homa, struct homa_rpc *rpc);
extern int      homa_message_in_copy_data(struct homa_message_in *msgin,
			struct iov_iter *iter, int max_bytes);
extern void     homa_message_in_destroy(struct homa_message_in *msgin);
extern void     homa_message_in_init(struct homa_message_in *msgin, int length,
			int incoming);
extern void     homa_message_out_destroy(struct homa_message_out *msgout);
extern void     homa_message_out_init(struct homa_rpc *rpc, int sport,
			struct sk_buff *skb, int len);
extern int      homa_message_out_reset(struct homa_rpc *rpc);
extern int      homa_metrics_open(struct inode *inode, struct file *file);
extern ssize_t  homa_metrics_read(struct file *file, char __user *buffer,
			size_t length, loff_t *offset);
extern int      homa_metrics_release(struct inode *inode, struct file *file);
extern int      homa_offload_end(void);
extern int      homa_offload_init(void);
extern void     homa_outgoing_sysctl_changed(struct homa *homa);
extern int      homa_pacer_main(void *transportInfo);
extern void     homa_pacer_stop(struct homa *homa);
extern void     homa_pacer_xmit(struct homa *homa);
extern void     homa_peer_abort(struct homa *homa, __be32 addr, int error);
extern void     homa_peertab_destroy(struct homa_peertab *peertab);
extern int      homa_peertab_init(struct homa_peertab *peertab);
extern struct homa_peer
               *homa_peer_find(struct homa_peertab *peertab, __be32 addr,
			struct inet_sock *inet);
extern void     homa_peer_set_cutoffs(struct homa_peer *peer, int c0, int c1,
			int c2, int c3, int c4, int c5, int c6, int c7);
extern void     homa_pkt_dispatch(struct sk_buff *skb, struct homa_sock *hsk);
extern int      homa_pkt_recv(struct sk_buff *skb);
extern __poll_t homa_poll(struct file *file, struct socket *sock,
			struct poll_table_struct *wait);
extern char    *homa_print_ipv4_addr(__be32 addr);
extern char    *homa_print_metrics(struct homa *homa);
extern char    *homa_print_packet(struct sk_buff *skb, char *buffer, int buf_len);
extern char    *homa_print_packet_short(struct sk_buff *skb, char *buffer,
			int buf_len);
extern int      homa_proc_read_metrics(char *buffer, char **start, off_t offset,
			int count, int *eof, void *data);
extern int      homa_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			int noblock, int flags, int *addr_len);
extern void     homa_rehash(struct sock *sk);
extern void     homa_remove_from_grantable(struct homa *homa,
			struct homa_rpc *rpc);
extern void     homa_resend_data(struct homa_rpc *rpc, int start, int end,
			int priority);
extern void     homa_resend_pkt(struct sk_buff *skb, struct homa_rpc *rpc,
			struct homa_sock *hsk);
extern void     homa_restart_pkt(struct sk_buff *skb, struct homa_rpc *rpc);
extern void     homa_rpc_abort(struct homa_rpc *crpc, int error);
extern void     homa_rpc_free(struct homa_rpc *rpc);
extern void     homa_rpc_free_rcu(struct rcu_head *rcu_head);
extern struct homa_rpc
               *homa_rpc_new_client(struct homa_sock *hsk,
		struct sockaddr_in *dest, void __user *buffer, size_t len);
extern struct homa_rpc
               *homa_rpc_new_server(struct homa_sock *hsk, __be32 source,
			struct data_header *h);
extern void     homa_rpc_ready(struct homa_rpc *rpc);
extern int      homa_rpc_reap(struct homa_sock *hsk);
extern int      homa_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);
extern int      homa_sendpage(struct sock *sk, struct page *page, int offset,
			size_t size, int flags);
extern int      homa_setsockopt(struct sock *sk, int level, int optname,
			char __user *optval, unsigned int optlen);
extern int      homa_shutdown(struct socket *sock, int how);
extern int      homa_snprintf(char *buffer, int size, int used,
			const char* format, ...)
			__attribute__((format(printf, 4, 5)));
extern int      homa_sock_bind(struct homa_socktab *socktab,
			struct homa_sock *hsk, __u16 port);
extern void     homa_sock_destroy(struct homa_sock *hsk);
extern struct homa_sock *
	        homa_sock_find(struct homa_socktab *socktab, __u16 port);
extern void     homa_sock_init(struct homa_sock *hsk, struct homa *homa);
extern void     homa_sock_shutdown(struct homa_sock *hsk);
extern int      homa_socket(struct sock *sk);
extern void     homa_socktab_destroy(struct homa_socktab *socktab);
extern void     homa_socktab_init(struct homa_socktab *socktab);
extern struct homa_sock
               *homa_socktab_next(struct homa_socktab_scan *scan);
extern struct homa_sock
               *homa_socktab_start_scan(struct homa_socktab *socktab,
			struct homa_socktab_scan *scan);
extern void     homa_spin(int usecs);
extern char    *homa_symbol_for_state(struct homa_rpc *rpc);
extern char    *homa_symbol_for_type(uint8_t type);
extern void     homa_tasklet_handler(unsigned long data);
extern void	homa_timer(struct homa *homa);
extern void     homa_unhash(struct sock *sk);
extern int      homa_unsched_priority(struct homa *homa,
			struct homa_peer *peer, int length);
extern int      homa_v4_early_demux(struct sk_buff *skb);
extern int      homa_v4_early_demux_handler(struct sk_buff *skb);
extern void     homa_validate_grantable_list(struct homa *homa, char *message);
extern struct homa_rpc
	       *homa_wait_for_message(struct homa_sock *hsk, int flags,
			__u64 id);
extern int      homa_xmit_control(enum homa_packet_type type, void *contents,
			size_t length, struct homa_rpc *rpc);
extern int      __homa_xmit_control(void *contents, size_t length,
			struct homa_peer *peer, struct homa_sock *hsk);
extern void     homa_xmit_data(struct homa_rpc *rpc, bool force);
extern void     __homa_xmit_data(struct sk_buff *skb, struct homa_rpc *rpc,
			int priority);


/*
  DCACP logic
*/
extern void dcacp_notification_pkt(struct sk_buff *skb);
extern void dcacp_rts_pkt(struct sk_buff *skb);
extern void dcacp_grant_pkt(struct sk_buff* skb);
extern void dcacp_accept_pkt(struct sk_buff* skb);



/**
 * check_pacer() - This method is invoked at various places in Homa to
 * see if the pacer needs to transmit more packets and, if so, transmit
 * them. It's needed because the pacer thread may get descheduled by
 * Linux, result in output stalls.
 * @homa:    Overall data about the Homa protocol implementation. No locks
 *           should be held when this function is invoked.
 * @softirq: Nonzero means this code is running at softirq (bh) level;
 *           zero means it's running in process context.
 */
static inline void check_pacer(struct homa *homa, int softirq)
{
	if (list_first_or_null_rcu(&homa->throttled_rpcs,
			struct homa_rpc, throttled_links) == NULL)
		return;
	if ((get_cycles() + homa->max_nic_queue_cycles) <
			atomic64_read(&homa->link_idle_time))
		return;
	homa_pacer_xmit(homa);
}

#endif /* _HOMA_IMPL_H */
