/*
 * Perfect hash function generator for PostgreSQL SQLSTATEs.
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include "postgres.h"
#include "utils/elog.h"
#undef qsort
#include <time.h>

#ifndef VERIFYFUNC

static inline unsigned int
hashm(unsigned int h1, unsigned int modulo, unsigned int c1, unsigned int c2)
{
  h1 ^= h1 >> 16;
  h1 *= 0x85ebca6b + c1;
  h1 ^= h1 >> 13;
  h1 *= 0xc2b2ae35 + c2;
  h1 ^= h1 >> 16;
  return h1 % modulo;
}

static int
getperfect(unsigned int *nums, unsigned int cnt, unsigned int modulo)
{
  unsigned int best = cnt, c1, best_c1 = 0, best_c2 = 0, iters = 0;
  time_t now;

#pragma omp parallel for reduction(+:iters)
  for (c1=0; c1<10000; c1++)
    {
      unsigned int result[modulo], c2, i, collisions;
      if (best == 0)
        continue;
      for (c2=0; c2<10000; c2++)
        {
          iters ++;
          collisions = 0;
          memset(result, 0xff, sizeof(result));
          for (i=0; i<cnt; i++)
            {
              unsigned int h = hashm(nums[i], modulo, c1, c2);
              if (result[h] != 0xffffffff)
                collisions ++;
              else
                result[h] = i;
            }
#pragma omp critical
          {
            if (collisions == 0)
              {
                best = 0;
                best_c1 = c1;
                best_c2 = c2;
              }
            else if (collisions < best)
              best = collisions;
          }
          if (best == 0)
            break;
       }
    }
  fprintf(stderr, "%u iterations, modulo %u, best function had %u duplicates\n", iters, modulo, best);
  if (best > 0)
    return 1;
  now = time(NULL);
  printf(
    "/* Generated by gensqlstatehashfunc.c on %s */\n"
    "#define HASH_SQLSTATE_MODULO %u\n"
    "static inline unsigned int\n"
    "hash_sqlstate(unsigned int s)\n"
    "{\n"
    "  s ^= s >> 16;\n"
    "  s *= 0x85ebca6b + %u;\n"
    "  s ^= s >> 13;\n"
    "  s *= 0xc2b2ae35 + %u;\n"
    "  s ^= s >> 16;\n"
    "  return s %% HASH_SQLSTATE_MODULO;\n"
    "}\n",
    ctime(&now), modulo, best_c1, best_c2);
  return 0;
}

#else /* VERIFYFUNC */

#include "sqlstatehashfunc.c"

static int
verifyfunc(unsigned int *nums, unsigned int cnt)
{
  unsigned int result[HASH_SQLSTATE_MODULO], collisions = 0, i;

  memset(result, 0xff, sizeof(result));
  for (i=0; i<cnt; i++)
    {
      unsigned int h = hash_sqlstate(nums[i]);
      if (result[h] != 0xffffffff)
        collisions ++;
      else
        result[h] = i;
    }

  fprintf(stderr, "found %u collisions\n", collisions);
  return collisions ? 1 : 0;
}

#endif /* VERIFYFUNC */

static int
cmp_uints(const void *a, const void *b)
{
  unsigned int p1 = *(unsigned int *) a, p2 = *(unsigned int *) b;
  return (p1 > p2) ? 1 : (p2 > p1) ? -1 : 0;
}

int
main(int argc, char **argv)
{
  FILE *fp;
  char errcodes_h_path[1000], line[200], a, b, c, d, e;
  unsigned int nums[1000], cnt = 0, uniq_nums[1000], uniq_cnt = 0, i;
  unsigned int modulos[] = { 1409, 2027, 3061, 4583 };

  if (argc != 2)
    {
      fprintf(stderr, "usage: %s `pg_config --includedir-server`\n", argv[0]);
      return 1;
    }

  snprintf(errcodes_h_path, sizeof(errcodes_h_path), "%s/utils/errcodes.h", argv[1]);
  fp = fopen(errcodes_h_path, "r");
  if (fp == NULL)
    {
      perror(errcodes_h_path);
      return 1;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    if (sscanf(line, "#define ERRCODE_%*s MAKE_SQLSTATE('%c','%c','%c','%c','%c')",
               &a, &b, &c, &d, &e) == 5)
      nums[cnt++] = MAKE_SQLSTATE(a, b, c, d, e);
  fclose(fp);
  qsort(nums, cnt, sizeof(unsigned int), cmp_uints);
  for (i=0; i<cnt; i++)
    if (i == 0 || nums[i] != nums[i-1])
      uniq_nums[uniq_cnt++] = nums[i];
  fprintf(stderr, "input set size: %u\n", uniq_cnt);

#ifndef VERIFYFUNC
  for (i=0; i<sizeof(modulos)/sizeof(modulos[0]); i++)
    {
      int res = getperfect(uniq_nums, uniq_cnt, modulos[i]);
      if (res == 0)
        return 0;
    }
  return 1;
#else /* VERIFYFUNC */
  return verifyfunc(uniq_nums, uniq_cnt);
#endif /* VERIFYFUNC */
}
