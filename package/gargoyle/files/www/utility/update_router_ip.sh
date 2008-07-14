# This program is copyright � 2008 Eric Bishop and is distributed under the terms of the GNU GPL 
# version 2.0 with a special clarification/exception that permits adapting the program to 
# configure proprietary "back end" software provided that all modifications to the web interface
# itself remain covered by the GPL. 
# See http://gargoyle-router.com/faq.html#qfoss for more information

do_file_ip_replace()
{
	file="$1"
	oldip="$2"
	newip="$3"

	oldnet=$(echo "$oldip" | sed "s/\.[^\.]*$//g")
	newnet=$(echo "$newip" | sed "s/\.[^\.]*$//g")

	#echo "oldnet=$oldnet"
	#echo "newnet=$newnet"

	newfile="$file.update.tmp"
	cat "$file" | sed "s/$oldip/$newip/g" | sed "s/$oldnet/$newnet/g" > "$newfile"
	mv "$newfile" "$file"
}

do_file_ip_replace "/etc/ethers" "$1" "$2"
do_file_ip_replace "/etc/hosts" "$1" "$2"
do_file_ip_replace "/etc/firewall.user" "$1" "$2"
do_file_ip_replace "/etc/config/qos_gargoyle" "$1" "$2"


