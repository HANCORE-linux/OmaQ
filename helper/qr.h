#ifndef OMAQ_QR_H
#define OMAQ_QR_H

/* Write a PNG of an omaq://invite/… URL via system qrencode. 0 or -1. */
int omaq_qr_write_png(const char *url, const char *path);
int omaq_qr_path_ok(const char *path);

#endif
