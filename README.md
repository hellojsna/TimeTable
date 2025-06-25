# TimeTable

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/hellojsna/TimeTable/blob/main/TimeTable.ipynb)


It uses custom API based on NEIS OpenAPI for searching school & getting timetable information.

It may not work on Windows(only tested on macOS).

I had to use an additional proxy for send request to HTTPS server over HTTP.

So the basic structure is: Client - HTTPS Proxy - API Server - NEIS API
