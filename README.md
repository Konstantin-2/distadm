# Distributed Administrator
“Distributed Administrator” is to administer decentralized group of computers. Group of computers may contain any number of isolated computers and any number of LANs isolated from each other. Administrator can use any of these computers at any time to input commands. These commands are distributed to other computers via network (if computers are connected) or via flash/optical drives (if computers are not connected). Commands are: add/delete user accounts, distribute files, execute shell commands, check nodes statuses (antivirus, S.M.A.R.T.). Command execution results also distributed to other computers so can be viewed anywhere.

Command distribution paths may be complex. For example, administrator inputs new command to Computer #1 in LAN #1. As soon as Computer #2 in LAN #1 become online, command is copied there. After administrator use flash-drive to copy command from Computer #2 in LAN #1 to Computer #3 in LAN #2. After it command automatically copied to Computer #4 in LAN #2 and so on.

Main difficulty solved in “distadm” is to be sure that every command be delivered to every computer in group and that accumulation of commands will not lead to overflow i.e. executed commands should be deleted to restore disk space.

# Installation
Installation on computers with graphical interface

make clean

sudo make install -j

Installation on computers without graphical interface

make clean

sudo NO_X=y make install -j

Create Debian package for computers with graphical interface

sudo make clean

sudo make distadm.deb

Install Debian package for computers with graphical interface

sudo apt install ./distadm.deb

Create Debian package for computers without graphical interface

sudo make clean

sudo make distadm-console.deb

Install Debian package for computers without graphical interface

sudo apt install ./distadm-console.deb

# Create and fill new group
## Create new group
Run distadm and press “Create new group” button. Or run

$ sudo distadm -I

## Add new computer in LAN
On any computer in group run distadm and press “Write online invite” button. Or run

$ sudo distadm write-online-invite &lt;filename&gt;

Then move created file to new computer to join to group.

Then on new computer run distadm and press “Join to group” button. Or run

$ sudo distadm -J &lt;filename&gt;

## Add new isolated computer or computer in other LAN
On any computer in group run distadm and press “Write offline invite” button. Or run

$ sudo distadm write-offline-invite &lt;filename&gt;

Then move created file to new computer to join to group.

Then on new computer run distadm and press “Join to group” button. Or run

$ sudo distadm -J &lt;filename&gt;

Invite-file will be modified. It can be used to join many computers sequentially. At the end move invite-file to first computer, run distadm and press “Finalize invite” button. Or run

$ sudo distadm finalize-invite &lt;filename&gt;

# Distribute commands between isolated segments

On any computer in group run distadm and press “Write packet” button. Or run

$ sudo distadm write-packet &lt;filename&gt;

Move created file to isolated computer or computer in isolated LAN, run distadm and press “Read packet” button. Or run

$ sudo distadm read-packet &lt;filename&gt;

Do not forget do the same backwards to move execution results to the first computers.

# More

For more information see “man distadm”.
