#pragma once

void net_pal_init(void);
void net_pal_teardown(void);

u64 net_pal_total_resolves(void);
u64 net_pal_total_connects(void);
u64 net_pal_total_bytes_read(void);
u64 net_pal_total_bytes_write(void);
