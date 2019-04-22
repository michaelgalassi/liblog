/*
 * llcat -- display logs created by liblog
 *
 * Copyright 2012-2019 Michael Galassi
 */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "liblog.h"

/*
 * enable printing buffer pointers, shouldn't need this ever again
#define DEBUG
 */

static void
usage(void)
{
   fprintf(stderr, "usage: llcat <file>\n");
   exit(1);
}

int
main(int argc, char **argv)
{
   lls *lp;
   struct stat sb;
   int fd;

   if (argc != 2) {
      usage();
      /*NOTREACHED*/
   }
   if ((fd = open(argv[1], O_RDONLY, 0)) == -1) {
      fprintf(stderr, "open of %s failed: %m\n", argv[1]);
      usage();
   }
   if (fstat(fd, &sb) == -1) {
      fprintf(stderr, "fstat failed: %m\n");
      close(fd);
      return 1;
   }
   if (!sb.st_size) {
      /* zero length is probably because lldump hasn't been tickled yet */
      close(fd);
      fprintf(stderr, "zero length file, (signal lldump?)\n");
      return 1;
   }
   if ((lp = (lls *)malloc(sb.st_size)) == (lls *)0) {
      fprintf(stderr, "malloc failed: %m\n");
      return 1;
   }
   if (read(fd, lp, sb.st_size) == -1) {
      fprintf(stderr, "read failed: %m\n");
      close(fd);
      free(lp);
      return 1;
   }
   (void)close(fd);
   if (!lp->buf[lp->head + 1] && lp->head < lp->len) {
      /*
       * if the next byte is a 0 we've not yet filled the buffer so
       * print from start to head
       */
      (void)fwrite(&lp->buf[0], 1, lp->head, stdout);
#ifdef DEBUG
      fprintf(stdout, "\nlp: len %lu head %lu\nfrom 0 for %lu\n",
              (unsigned long)lp->len, (unsigned long)lp->head, (unsigned long)lp->head);
#endif /*DEBUG*/
   } else {
      /*
       * we've wrapped one or more times so print from head to end,
       * then from start to head
       */
      (void)fwrite(&lp->buf[lp->head + 1], 1, lp->len - lp->head - 1, stdout);
      (void)fwrite(&lp->buf[0], 1, lp->head, stdout);
#ifdef DEBUG
      fprintf(stdout, "\nlp: len %lu head %lu\nfrom %lu for %lu\nfrom 0 for %lu\n",
              (unsigned long)lp->len, (unsigned long)lp->head, (unsigned long)lp->head + 1,
              (unsigned long)(lp->len - lp->head) - 1, (unsigned long)lp->head - 1);
#endif /*DEBUG*/
   }
   free(lp);
   return 0;
}
