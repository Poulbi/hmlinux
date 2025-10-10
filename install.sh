
CurrentDir="$(readlink -f ".")"

ScriptDir="$(dirname "$(readlink -f "$0")")"
cd "$ScriptDir"

Dir="$CurrentDir/linuxhmh"
rm -rf "$Dir"
mkdir "$Dir"
  
Files="$(find "$ScriptDir" -type f \( -iname '*.cpp' -o -iname '*.c' -o -iname '*.h' \))"

for File in $Files
do
 ln -sf "$File" "$Dir"
done
