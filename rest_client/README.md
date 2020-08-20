# rest_client: HTTP POST request in C

## Summary

This is piece of software that demonstrates using low-level socket functionality in C to send HTTP requests. POST requests of certain kind are used

    {"id":0, "jsonrpc":"2.0","method":"systemInfo","params":[]}

The number of methods is fixed and defined statically inline.

## Usage

The `rest_client` run syntax is

    ./rest_client [-i ID] [-j JSONRPC] URL METHOD [PARAMS]...
    
    -i ID, specifies ID for JSON request (0 used by default)
    -j JSONRPC, specifies JSONRPC value for request ("2.0" used by default)

    Example:
    
    ./rest_client -i 4 http://localhost:8000/api/json/v2 systeminfo
    
