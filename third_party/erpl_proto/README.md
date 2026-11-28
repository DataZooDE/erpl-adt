# Optional RFC dependency

RFC/SNC builds require authorized access to the private `DataZooDE/erpl-proto`
repository, pinned in `cmake/ErplProto.cmake`. Default public builds leave
`ERPL_ADT_WITH_RFC=OFF`. An authorized isolated checkout can be supplied with
`-DFETCHCONTENT_SOURCE_DIR_ERPL_PROTO=/path/to/erpl-proto` when enabling RFC.
Never embed access tokens in CMake arguments, URLs or cache files.

The dependency retains its BSL 1.1 license; enabling RFC does not relicense it
under Apache 2.0. Distribution of RFC-enabled binaries requires the release
owner to confirm the applicable terms. No private dependency source or SAP
crypto binaries are included here. The operator supplies CommonCryptoLib at
runtime; the SAP NW RFC SDK is not a build or runtime dependency.

Linux A4H SNC authentication and ADT edit/activation were tested without a
password. Windows Secure Login Client acceptance requires a separate live test.
