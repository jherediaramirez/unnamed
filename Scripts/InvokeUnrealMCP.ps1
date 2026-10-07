[CmdletBinding()]
param(
    [string]$Method = 'tools/list',
    [hashtable]$Parameters = @{},
    [uri]$Endpoint = 'http://127.0.0.1:18777/mcp',
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
if (-not $Endpoint.IsLoopback -or $Endpoint.Scheme -ne 'http') {
    throw 'This helper only connects to the local Unreal MCP server.'
}

$headers = @{ Accept = 'application/json, text/event-stream' }

function Send-McpRequest([hashtable]$Request) {
    $response = Invoke-WebRequest -Uri $Endpoint -Method Post -Headers $headers `
        -ContentType 'application/json' `
        -Body ($Request | ConvertTo-Json -Depth 50 -Compress) `
        -TimeoutSec $TimeoutSeconds
    if (-not $response.Content) {
        return @{ Response = $response; Payload = $null }
    }
    $content = $response.Content
    if ($response.Headers['Content-Type'] -like '*text/event-stream*') {
        $data = @($content -split "`r?`n" | Where-Object { $_.StartsWith('data:') })
        if (-not $data) { throw 'The MCP event stream contained no JSON response.' }
        $content = $data[-1].Substring(5).TrimStart()
    }
    $payload = $content | ConvertFrom-Json
    if ($payload.error) {
        throw "MCP error $($payload.error.code): $($payload.error.message)"
    }
    return @{ Response = $response; Payload = $payload }
}

$initialize = Send-McpRequest @{
    jsonrpc = '2.0'
    id = 1
    method = 'initialize'
    params = @{
        protocolVersion = '2025-11-25'
        capabilities = @{}
        clientInfo = @{ name = 'unnamed-project-tools'; version = '1.0' }
    }
}
$sessionId = $initialize.Response.Headers['Mcp-Session-Id'] | Select-Object -First 1
if ($sessionId) { $headers['Mcp-Session-Id'] = $sessionId }
$headers['MCP-Protocol-Version'] = $initialize.Payload.result.protocolVersion

try {
    $null = Send-McpRequest @{ jsonrpc = '2.0'; method = 'notifications/initialized' }
    $reply = Send-McpRequest @{
        jsonrpc = '2.0'
        id = 2
        method = $Method
        params = $Parameters
    }
    if ($reply.Payload.result.isError) {
        throw ($reply.Payload.result.content.text -join "`n")
    }
    $reply.Payload.result
}
finally {
    if ($sessionId) {
        # MCP servers may decline explicit session termination.
        try {
            $null = Invoke-WebRequest -Uri $Endpoint -Method Delete -Headers $headers `
                -TimeoutSec $TimeoutSeconds -SkipHttpErrorCheck
        }
        catch {
            # Closing an editor can also close the transport before session cleanup.
        }
    }
}
