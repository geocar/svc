# normally you'll call this with make args...
# make common/lock.c
# make unix/ttywrap.c
# make common/lock.c
all: sleep-svc wait-trigger pull-trigger ttywrap lockf noroot chdirhome
sleep-svc: unix/sleep-svc.c common/lock.c
wait-trigger: unix/wait-trigger.c
pull-trigger: unix/pull-trigger.c
ttywrap: unix/ttywrap.c
lockf: unix/lockf.c common/lock.c
noroot: 
chdirhome: 
