//%attributes = {}
C_TEXT:C284($1;$dom)
C_COLLECTION:C1488($2;$urls;$0)

$dom:=$1
$urls:=$2

If ($urls=Null:C1517)
	$urls:=New collection:C1472
End if 

ARRAY TEXT:C222($doms;0)
ARRAY LONGINT:C221($types;0)

DOM GET XML CHILD NODES:C1081($dom;$types;$doms)

C_TEXT:C284($name;$value)

For ($i;1;Size of array:C274($types))
	$type:=$types{$i}
	
	If ($type=XML ELEMENT:K45:20)
		$dom:=$doms{$i}
		
		For ($ii;1;DOM Count XML attributes:C727($dom))
			DOM GET XML ATTRIBUTE BY INDEX:C729($dom;$ii;$name;$value)
			If ($name="href")
				$urls.push($value)
			End if 
		End for 
		
		parse_href ($dom;$urls)
		
	End if 
End for 

$0:=$urls