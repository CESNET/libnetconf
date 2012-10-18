/**
 * \file notifications.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions to handle NETCONF Notifications.
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#ifndef NOTIFICATIONS_H_
#define NOTIFICATIONS_H_

#include "netconf.h"

#define NCNTF_STREAM_BASE "netconf"

/**
 * @ingroup notifications
 * @brief Enumeration of supported NETCONF event notifications.
 */
typedef enum {
	NCNTF_ERROR = -1, /**< error return code */
	NCNTF_GENERIC = 0, /**< generic notification not directly supported by libnetconf */
	NCNTF_REPLAY_COMPLETE = 1, /**< \<replayComplete\> notification announcing end of Replaying the stream */
	NCNTF_NTF_COMPLETE = 2, /**< \<notificationComplete\> notification announcing end of Notification stream */
	NCNTF_BASE_CFG_CHANGE = 3, /**< netconf-config-change (RFC 6470) */
	NCNTF_BASE_CPBLT_CHANGE = 4, /**< netconf-capability-change (RFC 6470) */
	NCNTF_BASE_SESSION_START = 5, /**< netconf-session-start (RFC 6470) */
	NCNTF_BASE_SESSION_END = 6, /**< netconf-session-end (RFC 6470) */
	NCNTF_BASE_CONFIRMED_COMMIT = 7 /**< netconf-configrmed-commit (RFC 6470) */
} NCNTF_EVENT;

/**
 * @ingroup notifications
 * @brief Enumeration of possible source of events
 */
typedef enum {
	NCNTF_EVENT_BY_SERVER, /**< event is caused by server */
	NCNTF_EVENT_BY_USER /**< event is caused by user's action */
}NCNTF_EVENT_BY;

/**
 * @ingroup notifications
 * @brief Initiate NETCONF Notifications environment
 * @return 0 on success, non-zero value else
 */
int ncntf_init(void);

/**
 * @ingroup notifications
 * @brief Close all NETCONF Event Streams and other parts of the Notifications
 * environment.
 */
void ncntf_close(void);

/**
 * @ingroup notifications
 * @brief Create new \<notification\> message with givent eventTime and content.
 *
 * @param[in] event_time Time of the event.
 * @param[in] content Description of the event in the XML form.
 * @return Created notification message.
 */
nc_ntf* ncntf_notif_create(time_t event_time, const char* content);

/**
 * @ingroup notifications
 * @brief Free notification message.
 * @param[in] ntf notification message to free.
 */
void ncntf_notif_free(nc_ntf *ntf);

/**
 * @ingroup notifications
 * @brief Create new NETCONF event stream.
 * @param[in] name Name of the stream.
 * @param[in] desc Description of the stream.
 * @param[in] replay Specify if the replay is allowed (1 for yes, 0 for no).
 * @return 0 on success, non-zero value else.
 */
int ncntf_stream_new(const char* name, const char* desc, int replay);

/**
 * @ingroup notifications
 * @brief Get the list of NETCONF event notifications streams.
 * @return NULL terminated list of stream names. It is up to caller to free the
 * list
 */
char** ncntf_stream_list(void);

/**
 * @ingroup notifications
 * @brief Test if the given stream is already created and available
 * @param[in] name Name of the stream to check.
 * @return 0 - the stream is not present,<br/>1 - the stream is present
 */
int ncntf_stream_isavailable(const char* name);

/**
 * @ingroup notifications
 * @brief Store new event into the specified stream. Parameters are specific
 * for different events.
 *
 * ### Event parameters:
 * - #NCNTF_GENERIC
 *  - **const char* content** Content of the notification as defined in RFC 5277.
 *  eventTime is added automatically. The string should be XML formatted.
 * - #NCNTF_BASE_CFG_CHANGE
 *  - #NC_DATASTORE **datastore** Specify which datastore has changed.
 *  - #NCNTF_EVENT_BY **changed_by** Specify the source of the change.
 *   - If the value is set to #NCNTF_EVENT_BY_USER, following parameter is
 *   required:
 *  - **const struct nc_session* session** Session required the configuration change.
 * - #NCNTF_BASE_CPBLT_CHANGE
 *  - **const struct nc_cpblts* old** Old list of capabilities.
 *  - **const struct nc_cpblts* new** New list of capabilities.
 *  - #NCNTF_EVENT_BY **changed_by** Specify the source of the change.
 *   - If the value is set to #NCNTF_EVENT_BY_USER, following parameter is
 *   required:
 *  - **const struct nc_session* session** Session required the configuration change.
 * - #NCNTF_BASE_SESSION_START
 *  - **const struct nc_session* session** Started session (#NC_SESSION_STATUS_DUMMY session is also allowed).
 * - #NCNTF_BASE_SESSION_END
 *  - **const struct nc_session* session** Finnished session (#NC_SESSION_STATUS_DUMMY session is also allowed).
 *  - #NC_SESSION_TERM_REASON **reason** Session termination reason.
 *   - If the value is set to #NC_SESSION_TERM_KILLED, following parameter is
 *   required.
 *  - **const char* killed-by-sid** The ID of the session that directly caused
 *  the session termination. If the session was terminated by a non-NETCONF
 *  process unknown to the server, use NULL as the value.
 *
 * ### Examples:
 * - nc_ntf_event_new("mystream", -1, NCNTF_GENERIC, "<event>something happend</event>");
 * - nc_ntf_event_new("netconf", -1, NCNTF_BASE_CFG_CHANGE, NC_DATASTORE_RUNNING, NCNTF_EVENT_BY_USER, my_session);
 * - nc_ntf_event_new("netconf", -1, NCNTF_BASE_CPBLT_CHANGE, old_cpblts, new_cpblts, NCNTF_EVENT_BY_SERVER);
 * - nc_ntf_event_new("netconf", -1, NCNTF_BASE_SESSION_START, my_session);
 * - nc_ntf_event_new("netconf", -1, NCNTF_BASE_SESSION_END, my_session, NC_SESSION_TERM_KILLED, "123456");
 *
 * @param[in] stream Name of the stream where the event will be stored.
 * @param[in] etime Time of the event, if set to -1, current time is used.
 * @param[in] event Event type to distinguish following parameters.
 * @param[in] ... Specific parameters for different event types as described
 * above.
 * @return 0 for success, non-zero value else.
 */
int ncntf_event_new(char* stream, time_t etime, NCNTF_EVENT event, ...);

/**
 * \todo: thread safety (?thread-specific variables)
 * @ingroup notifications
 * @brief Start iteration on the events in the specified stream file. Iteration
 * starts on the first event in the first part of the stream file.
 * @param[in] stream Name of the stream to iterate.
 */
void ncntf_stream_iter_start(const char* stream);

/**
 * \todo: thread safety (?thread-specific variables)
 * @ingroup notifications
 * @brief Pop the next event record from the stream file. The iteration must be
 * started by nc_ntf_stream_iter_start() function.
 * @param[in] stream Name of the stream to iterate.
 * @param[in] start Time of the first event caller is interested in.
 * @param[in] stop Time of the last event caller is interested in.
 * @param[out] event_time Time of the returned event, NULL if caller does not care.
 * @return Content of the next event in the stream.
 */
char* ncntf_stream_iter_next(const char* stream, time_t start, time_t stop, time_t *event_time);

/**
 * \todo: thread safety (?thread-specific variables)
 * @ingroup notifications
 * @brief Clean all structures used for iteration in the specified stream. This
 * function must be called as a closing function to nc_ntf_stream_iter_start()
 * @param[in] stream Name of the iterated stream.
 */
void ncntf_stream_iter_finnish(const char* stream);

/**
 * @ingroup notifications
 * @brief Get Time of the event reported in the notification message.
 * @param[in] notif Notification message.
 * @return Time of the event (as number of seconds since epoch).
 */
time_t ncntf_notif_get_time(nc_ntf* notif);

/**
 * @ingroup notifications
 * @brief Get description of the event reported in the notification message.
 * @param[in] notif Notification message.
 * @return Content of the event description (serialized XML).
 */
char* ncntf_notif_get_content(nc_ntf* notif);

/**
 * @ingroup notifications
 * @brief Check validity of \<create-subscription\> message.
 *
 * This check is done by ncntf_dispatch_send() which returns -1 when test does
 * not pass. However, it can be sometime useful to run this test before calling
 * ncntf_dispatch_send().
 *
 * @param[in] subscribe_rpc \<create-subscription\> RPC.
 * @return Reply message to the subscription - ok if tests passed and reply-error
 * with problem description if any of the tests fails.
 */
nc_reply *ncntf_check_subscription(const nc_rpc* subscribe_rpc);

/**
 * @ingroup notifications
 * @brief Start sending notification according to the given
 * \<create-subscription\> NETCONF RPC request. All events from the specified
 * stream are processed and sent to the client until the stop time is reached
 * or until the session is terminated.
 *
 * @param[in] session NETCONF session where the notifications will be sent.
 * @param[in] subscribe_rpc \<create-subscription\> RPC, if any other RPC is
 * given, -1 is returned.
 *
 * @return number of sent notifications (including 0), -1 on error.
 */
long long int ncntf_dispatch_send(struct nc_session* session, const nc_rpc* subscribe_rpc);

/**
 * @ingroup notifications
 * @brief Subscribe for receiving notifications from the given session
 * according to parameters in the given subscribtion RPC. Received notifications
 * are processed by the given process_ntf callback function. Functions stops
 * when the final notification \<notificationComplete\> is received or when the
 * session is terminated.
 *
 * @param[in] session NETCONF session where the notifications will be sent.
 * @param[in] subscribe_rpc \<create-subscription\> RPC, if any other RPC is
 * given, -1 is returned.
 * @param[in] process_ntf Callback function to process content of the
 * notification. If NULL, content of the notification is printed on stdout.
 *
 * @return number of received notifications, -1 on error.
 */
long long int ncntf_dispatch_receive(struct nc_session *session, const nc_rpc* subscribe_rpc, void (*process_ntf)(time_t eventtime, const char* content));

#endif /* NOTIFICATIONS_H_ */
