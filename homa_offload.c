/* Copyright (c) 2019, Stanford University
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

/* This file implements GSO (Generic Ssegmentation Offload) and GRO (Generic
 * Receive Offload) for Homa.
 */

#include "homa_impl.h"

static const struct net_offload homa_offload = {
	.callbacks = {
		.gso_segment	=	NULL,
		.gro_receive	=	homa_gro_receive,
		.gro_complete	=	homa_gro_complete,
	},
};

extern struct homa *homa;

/**
 * homa_offload_init() - Invoked to enable GRO and GSO. Typically invoked
 * when the Homa module loads.
 * Return: nonzero means error.
 */
int homa_offload_init(void)
{
	return inet_add_offload(&homa_offload, IPPROTO_HOMA);
}

/**
 * homa_offload_end() - Disables GRO and GSO for Homa; typically invoked
 * during Homa module unloading.
 * 
 * Return: nonzero means error.
 */
int homa_offload_end(void)
{
	return inet_del_offload(&homa_offload, IPPROTO_HOMA);
}

/**
 * homa_gro_receive() - Invoked for each input packet at a very low
 * level in the stack to perform GRO. However, this code does GRO in an
 * unusual way: it simply aggregates all packets targeted to a particular
 * destination port, so that the entire bundle can get through the networking
 * stack in a single traversal.
 * @gro_list:   Pointer to pointer to first in list of packets that are being
 *              held for possible GRO merging.
 * @skb:        The newly arrived packet.
 * 
 * Return: If the return value is non-NULL, it refers to a link in
 * gro_list. The skb referred to by that link should be removed from the
 * list by the caller and passed up the stack immediately. This function
 * always returns NULL.
 */
struct sk_buff *homa_gro_receive(struct list_head *gro_list, struct sk_buff *skb)
{
	/* This function will do one of the following things:
	 * 1. Merge skb with a packet in gro_list by appending it to
	 *    the frag_list of that packet.
	 * 2. Set NAPI_GRO_CB(skb)->flush, indicating that skb is not a
	 *    candidate for merging and should be passed up the networking
	 *    stack immediately.
	 * 3. Leave skb untouched, in which case it will be added to
	 *    gro_list by the caller, so it will be considered for merges
	 *    in the future.
	 */
	struct common_header *h_new;
	int hdr_offset, hdr_end;
	// struct sk_buff *held_skb;
	struct sk_buff *pp = NULL;
	
	/* Get access to the Homa header for the packet. I don't understand
	 * why such ornate code is needed, but this mimics what TCP does.
	 */
	hdr_offset = skb_gro_offset(skb);
	hdr_end = hdr_offset + sizeof32(*h_new);
	h_new = (struct common_header *) skb_gro_header_fast(skb, hdr_offset);
	if (skb_gro_header_hard(skb, hdr_end)) {
		h_new = (struct common_header *) skb_gro_header_slow(skb, hdr_end,
				hdr_offset);
		if (unlikely(!h_new)) {
			/* Header not available in contiguous memory. */
			UNIT_LOG(";", "no header");
			goto flush;
		}
	}
	
	h_new->gro_count = 1;
	list_for_each_entry(pp, gro_list, list) {
		struct common_header *h_held;
		if (!NAPI_GRO_CB(pp)->same_flow)
			continue;

		h_held = (struct common_header *) skb_transport_header(pp);

		/* Note: Homa will aggregate packets from different RPCs
		 * and different ports in order to maximize the benefits
	         * of GRO.
	         */
		
		/* Aggregate skb into pp. We don't update the length of
		 * pp, because we'll eventually split it up and process
		 * each skb independently.
		 */
		if (NAPI_GRO_CB(pp)->last == pp)
			skb_shinfo(pp)->frag_list = skb;
		else
			NAPI_GRO_CB(pp)->last->next = skb;
		NAPI_GRO_CB(pp)->last = skb;
		skb->next = NULL;
		NAPI_GRO_CB(skb)->same_flow = 1;
		NAPI_GRO_CB(pp)->count++;
		h_held->gro_count++;
		if (h_held->gro_count >= homa->max_gro_skbs)
			return pp;
	        break;
	}
	return NULL;
	// for (pp = gro_list; (held_skb = *pp) != NULL; pp = &held_skb->next) {
	// 	struct common_header *h_held;
	// 	if (!NAPI_GRO_CB(held_skb)->same_flow)
	// 		continue;

	// 	h_held = (struct common_header *) skb_transport_header(held_skb);

	// 	/* Note: Homa will aggregate packets from different RPCs
	// 	 * and different ports in order to maximize the benefits
	//          * of GRO.
	//          */
		
	// 	 Aggregate skb into held_skb. We don't update the length of
	// 	 * held_skb, because we'll eventually split it up and process
	// 	 * each skb independently.
		 
	// 	if (NAPI_GRO_CB(held_skb)->last == held_skb)
	// 		skb_shinfo(held_skb)->frag_list = skb;
	// 	else
	// 		NAPI_GRO_CB(held_skb)->last->next = skb;
	// 	NAPI_GRO_CB(held_skb)->last = skb;
	// 	skb->next = NULL;
	// 	NAPI_GRO_CB(skb)->same_flow = 1;
	// 	NAPI_GRO_CB(held_skb)->count++;
	// 	h_held->gro_count++;
	// 	if (h_held->gro_count >= homa->max_gro_skbs)
	// 		return pp;
	//         break;
	// }
	// return NULL;
	
flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}


/**
 * homa_gro_complete() - This function is invoked just before a packet that
 * was held for GRO processing is passed up the network stack, in case the
 * protocol needs to do some cleanup on the merged packet. Right now there
 * is nothing to do.
 * @skb:     The packet for which GRO processing is now finished.
 * @hoffset: Offset within the packet of the transport header.
 *
 * Return:   Always returns 0, signifying success. 
 */
int homa_gro_complete(struct sk_buff *skb, int hoffset)
{
	struct common_header *h = (struct common_header *)
			skb_transport_header(skb);
	struct data_header *d = (struct data_header *) h;
	tt_record3("homa_gro_complete type %d, id %d, offset %d",
			h->type, h->id, ntohl(d->seg.offset));
	
	/* Set the hash for the skb, which will be used for RPS (the default
	 * hash doesn't understand Homa, so it doesn't include port #'s).
	 * Setting the hash here is suboptimal, because this function doesn't
	 * get invoked for skb's where nothing was merged onto them.
	 * However, setting the hash in homa_gro_receive doesn't work either,
	 * because it messes up the same_flow computation, which will compare
	 * the default hash of a new packet with the recomputed hash of a
	 * held packet. 
	 */
	__skb_set_sw_hash(skb, jhash_3words(ip_hdr(skb)->saddr,
			h->sport, h->dport, 0), false);
	return 0;
}