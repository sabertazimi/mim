#!/bin/bash

stty -echo
read -p "[sudo] $(whoami)'s password: " password
stty echo

echo $password | sudo -S apt-get install -y libncurses5-dev
