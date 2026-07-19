/* jconfig.h — minimal configuration for cross-compiling against the device's
 * IJG libjpeg 9.x (libjpeg.so.9, JPEG_LIB_VERSION 90). We use the lib at
 * runtime via dlopen, so this only needs to satisfy the header's #ifdefs for
 * type sizes / char-signedness on the target (mipsel, glibc 2.22). */
#ifndef JCONFIG_INCLUDED
#define JCONFIG_INCLUDED

#define HAVE_STDDEF_H      1
#define HAVE_STDLIB_H      1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1

/* JPEG storage: 8-bit samples (the default; the device's libjpeg is built this
 * way). */
#define BITS_IN_JSAMPLE     8

/* stdio + memory sources are what we use (jpeg_stdio_src via dlsym). */
#define HAVE_STDIO_H        1

#endif /* JCONFIG_INCLUDED */