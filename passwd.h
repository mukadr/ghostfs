#ifndef GHOST_PASSWD_H
#define GHOST_PASSWD_H

struct sampler;
struct stegger;

int passwd_open(struct stegger **stegger, struct sampler *sampler, const char *password);

#endif
