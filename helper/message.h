#ifndef OMAQ_MESSAGE_H
#define OMAQ_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_MESSAGE_TEXT_MAX 1399u

int omaq_message_text_bytes_ok(const uint8_t *text, size_t length);
int omaq_message_id_new(char *out, size_t outn);
int omaq_message_id_ok(const char *id);
int omaq_message_id_reserved(const char *id);
int omaq_message_append_id_reply_at(const char *home, const char *conv_id,
				     const char *from, const char *text, const char *dir,
				     const char *message_id, const char *reply_id,
				     int64_t timestamp);
int omaq_message_append_id_reply(const char *home, const char *conv_id, const char *from,
				  const char *text, const char *dir, const char *message_id,
				  const char *reply_id);
int omaq_message_append_id(const char *home, const char *conv_id, const char *from,
			   const char *text, const char *dir, const char *message_id);
int omaq_message_append_with_id_at(const char *home, const char *conv_id,
				   const char *from, const char *text, const char *dir,
				   char *id_out, size_t id_outn, int64_t timestamp);
int omaq_message_append_with_id(const char *home, const char *conv_id, const char *from,
				const char *text, const char *dir, char *id_out, size_t id_outn);
int omaq_message_append_attachment_id_at(const char *home, const char *conv_id,
					 const char *from, const char *path,
					 const char *dir, const char *kind,
					 const char *message_id, int64_t timestamp);
int omaq_message_append_attachment_id(const char *home, const char *conv_id,
				      const char *from, const char *path,
				      const char *dir, const char *kind,
				      const char *message_id);
int omaq_message_append_attachment_with_id_at(const char *home, const char *conv_id,
					      const char *from, const char *path,
					      const char *dir, const char *kind,
					      char *id_out, size_t id_outn,
					      int64_t timestamp);
int omaq_message_append_attachment_with_id(const char *home, const char *conv_id,
					   const char *from, const char *path,
					   const char *dir, const char *kind,
					   char *id_out, size_t id_outn);
int omaq_message_append_file_with_id(const char *home, const char *conv_id,
				     const char *from, const char *path, const char *dir,
				     char *id_out, size_t id_outn);
int omaq_message_edit(const char *home, const char *conv_id, const char *id, const char *text);
int omaq_message_delete(const char *home, const char *conv_id, const char *id);
int omaq_message_apply_edit_from(const char *home, const char *conv_id, const char *id,
				 const char *text, const char *from);
int omaq_message_apply_delete_from(const char *home, const char *conv_id, const char *id,
				   const char *from);
int omaq_message_apply_edit(const char *home, const char *conv_id, const char *id, const char *text);
int omaq_message_apply_delete(const char *home, const char *conv_id, const char *id);
int omaq_message_append(const char *home, const char *conv_id, const char *from,
			const char *text, const char *dir);
int omaq_message_history(const char *home, const char *conv_id, int limit,
			 char **out, size_t *out_len);
int omaq_message_search(const char *home, const char *conv_id, const char *needle,
			int limit, char **out, size_t *out_len);

#endif
