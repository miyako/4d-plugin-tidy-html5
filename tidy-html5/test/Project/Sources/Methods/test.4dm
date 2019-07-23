//%attributes = {}
C_BLOB:C604($data)
CONVERT FROM TEXT:C1011("<title>Foo</title><p>Foo!";"utf-8";$data)

$options:=New object:C1471
$status:=Tidy ($data;$options)
