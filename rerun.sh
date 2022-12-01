export PSP_DIR=~/psp
${PSP_DIR}/scripts/setup/base_setup.sh
ps ax | grep psp-app | grep -v grep | awk '{print $1}' | xargs sudo kill -9
sudo numactl -N0 -m0 $PSP_DIR/build/src/c++/apps/app/psp-app --cfg ${PSP_DIR}/configs/base_psp_cfg.yml --label test