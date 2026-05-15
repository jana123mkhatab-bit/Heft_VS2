# Test script: Run scheduler with different task/VM ratios
# Collects algorithm performance across different configurations

$results = @()

$configs = @(
    @{tasks=10; vms=10; name="1:1 ratio"},    # balanced
    @{tasks=20; vms=5;  name="4:1 ratio"},    # many tasks few VMs
    @{tasks=5;  vms=10; name="1:2 ratio"},    # few tasks many VMs
    @{tasks=40; vms=4;  name="10:1 ratio"},   # high contention
    @{tasks=8;  vms=20; name="1:2.5 ratio"},  # many VMs
    @{tasks=40; vms=10; name="4:1 ratio"},    # moderate
)

foreach ($config in $configs) {
    Write-Host "================================" -ForegroundColor Cyan
    Write-Host "Testing: $($config.name) ($($config.tasks) tasks / $($config.vms) VMs)" -ForegroundColor Cyan
    Write-Host "================================" -ForegroundColor Cyan
    
    # Run HeftProject.exe with automated input
    $output = @($config.tasks, $config.vms) | .\HeftProject.exe 2>&1
    
    # Extract algorithm results from output
    $output | Select-String -Pattern "(HEFT|DP|EDP|Divide|Merge).*\|" | ForEach-Object {
        $line = $_.Line
        Write-Host $line
        $results += [PSCustomObject]@{
            Config = $config.name
            Tasks = $config.tasks
            VMs = $config.vms
            Line = $line
        }
    }
    Write-Host ""
}

Write-Host "`n========== SUMMARY ==========" -ForegroundColor Yellow
$results | Format-Table -AutoSize
