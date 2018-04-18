Get-ChildItem -Path . -Recurse -Filter *.gz | 
Foreach-Object {
    #$content = Get-Content $_.FullName

	echo $_.FullName
	
	& 'C:\Program Files\Bandizip\Bandizip.exe' bx -o:tif -y $_.FullName
    #filter and save content to the original file
    #$content | Where-Object {$_ -match 'step[49]'} | Set-Content $_.FullName

    #filter and save content to a new file 
    #$content | Where-Object {$_ -match 'step[49]'} | Set-Content ($_.BaseName + '_out.log')
}