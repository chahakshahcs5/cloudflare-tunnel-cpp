$cert = Get-ChildItem 'Cert:\CurrentUser\My' -CodeSigningCert | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=DevCloudflared' -CertStoreLocation 'Cert:\CurrentUser\My'
}

$files = Get-ChildItem 'build\Release\*.exe'
foreach ($f in $files) {
    Write-Host "Signing $($f.Name)..."
    Set-AuthenticodeSignature -FilePath $f.FullName -Certificate $cert | Out-Null
}
Write-Host "All executables signed successfully."
