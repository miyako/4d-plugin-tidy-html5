//%attributes = {}
$url:="https://www.tresor.economie.gouv.fr/services-aux-entreprises/sanctions-economiques/tout-savoir-sur-les-personnes-et-entites-sanctionnees"

C_BLOB:C604($data)
If (HTTP Get:C1157($url;$data)=200)
	
	$options:=New object:C1471("xmlOut";True:C214)
	$status:=Tidy ($data;$options)
	
	If ($status.status=1)
		
		$html:=Replace string:C233($status.html;"&nbsp;";"&#160;";*)  //is this a Tidy bug?
		$dom:=DOM Parse XML variable:C720($html)
		$urls:=parse_href ($dom;$urls)
		DOM CLOSE XML:C722($dom)
		
	End if 
	
End if 