#!/bin/sh
# Motorola Moto G06 (lagos). sxmo picks this up by the first DT compatible.

# Power and volume up are PMIC keys; volume down is on the keypad controller
export SXMO_POWER_BUTTON="1:1:mtk-pmic-keys"
export SXMO_VOLUME_BUTTON="1:1:mtk-pmic-keys 0:0:mtk-kpd"
export SXMO_TOUCHSCREEN_ID="0:0:chipone-tddi"
# 720x1640 panel
export SXMO_SWAY_SCALE="2"
# The modem isn't brought up yet
export SXMO_NO_MODEM=1
