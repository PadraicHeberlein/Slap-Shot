# Slap-Shot
Slap-Shot is essentially a firmware manager and LoRa-to-WiFi linker for the Puck System. It is a terminal-based, text GUI that allows each Puck to communicate with the Puck System GUI on the cloud.

When you first open Slap-Shot, make sure that both the Hockey Stick Base Station and any Pucks you have are unplugged.

> [! CAUTION]
> When prompted, the first thing you should do is update the firmware. After the firmware has been updated, you will be asked if you 
> have any Pucks to register. At this point, this has no implementaion, so just say no. Once we know that the software here works, 
> adding Puck registration on my end will take very little.

Next, you will be asked to enter your credentials for wour local WiFi network. After a successful connection, you will get to the main menu of the program.

> [! TIP]
> If you have trouble connecting to the WiFi, try moving closer to your router. The WiFi antenna on the base station isn't very
> strong, and it may not be recieving a signal.

Currently, the only option that works from the main menu is #3, and there is only on command, #1. This command will send a dummy event registration to the Puck System on the cloud.

Before entering the command, login to the [Puck System](https://staging.eas-defense.com/) with the dummy credentials provided by EAS. Once logged in, you can "ping" the system on th cloud with command #1. 

You should be able to see the event registration appear on the Puck System GUI.