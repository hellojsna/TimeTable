# TimeTable

It uses custom API based on NEIS OpenAPI for searching school & getting timetable information.

It may not work on Windows(only tested on macOS).

I had to use an additional proxy for send request to HTTPS server over HTTP.

So the basic structure is: Client - HTTPS Proxy - API Server - NEIS API