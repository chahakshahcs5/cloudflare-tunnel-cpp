$cert = (Get-AuthenticodeSignature 'build\Release\test_platform.exe').SignerCertificate
if ($cert) {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store([System.Security.Cryptography.X509Certificates.StoreName]::TrustedPeople, [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $store.Add($cert)
    $store.Close()
    Write-Host "Cert added to CurrentUser\TrustedPeople"
}
Get-AuthenticodeSignature 'build\Release\cloudflared.exe'
