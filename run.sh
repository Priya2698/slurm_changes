#!bin/bash
cd /home/priya/project/slurm-19.05.0
echo " Running make "
make 
echo " Running make install "
sudo make install
echo " Restarting and checking slurmctld status"
sudo systemctl restart slurmctld
sudo systemctl status slurmctld
echo " Restarting and checking slurmd status"
sudo systemctl restart slurmd
sudo systemctl status slurmd
