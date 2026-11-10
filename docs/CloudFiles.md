# Society Cloud Files retirement

The per-account cloud Files allowance is discontinued. Society no longer shows
the former storage navigation or offers uploads and replacements. This does not
remove the local `Society.societycontainer`, configured device synchronization,
account sessions, or previously recorded cloud files. No bucket or customer
file is deleted by the retirement change.

The existing controller remains available to integration code for listing and
recovering recorded files after a compatible server has been deployed. It refuses
new uploads and completion requests locally, including when the server is older.
Previously started uploads may still be cancelled where the server supports it.
The controller's download path confines the destination to the local Files
section, verifies the expected size, uses a separate cookie-free S3 transport,
and publishes without replacing a local file.

The discontinued panel has been removed. An LVUI recovery screen was designed
in the Society Figma file, but it has not been implemented or installed. For
the server-side recovery contract and its deployment limitations, see
`Service/iisacc.com/docs/SOCIETY_FILES.md`.

`Society.CloudFiles` tests the upload refusal, account/session isolation,
download transport, and preservation of existing local bytes with a fixture
account server, fake transfer service, and a disposable container. A local build
or fixture test does not establish that the installed app or production server
has adopted the change.
