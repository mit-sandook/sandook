#!/bin/python

# All our 100 ports (retrieved from ucli using the commands "pm show" and
# "port_mgr ports").
dev_ports=[40, 32, 24, 16, 8, 0, 44, 60, 176, 160, 136, 128]
pipes=[0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1]
ppg=0

bfrt.tf1.tm.pool.cfg.mod(pool='EG_APP_POOL_1',size_cells=200000)

for i in range(len(dev_ports)):
  port=dev_ports[i]
  pp=pipes[i]
  ppg+=1
  # configure ingress
  bfrt.tf1.tm.ppg.cfg.add_with_dev_port(pipe=pp, ppg_id=ppg, dev_port=port,
                                        icos_0=True, icos_4=True,
					guaranteed_cells=0x14,
					hysteresis_cells=0x80,
					pool_id='IG_APP_POOL_0',
					pool_max_cells=1536,
					pfc_enable=True, pfc_skid_max_cells=500)
  pg=(bfrt.tf1.tm.port.cfg.get(pipe=pp,dev_port=port).data)[b'pg_id']
  # configure egress
  set_shared_pool_fn=lambda icos: \
    bfrt.tf1.tm.queue.buffer.mod_with_shared_pool(pipe=pp, pg_id=pg,
                                                pg_queue=icos,
						guaranteed_cells=20,
						pool_id='EG_APP_POOL_1',
						pool_max_cells=20000,
						dynamic_baf='DISABLE')
  set_shared_pool_fn(0)
  set_shared_pool_fn(4)
  # enable PFC
  bfrt.tf1.tm.port.flowcontrol.mod(dev_port=port, mode_tx='PFC', mode_rx='PFC',
                                   cos_to_icos=[0, 1, 2, 3, 4, 5, 6, 7])
  bfrt.port.port.mod(DEV_PORT=port,TX_PFC_EN_MAP=0xFFFFFFFF,RX_PFC_EN_MAP=0xFFFFFFFF)
