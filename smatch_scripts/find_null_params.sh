grep " unchecked " warns.txt | cut -d ' ' -f 5- | sort -u > unchecked
grep " undefined " warns.txt | cut -d ' ' -f 5- | sort -u > null_calls.txt
cat null_calls.txt unchecked | sort | uniq -d > null_params.txt
IFS="
"
for i in $(cat null_params.txt) ; do
	grep "$i" warns.txt | grep -w undefined 
done | tee null_probs.txt


