# Rebuild the font atlas character list after changing interface or card text.
$project_root = Split-Path -Parent $PSScriptRoot
$text_files = @(
    (Join-Path $project_root 'apps/client/main.cpp'),
    (Join-Path $project_root 'src/core/game.cpp'),
    (Join-Path $project_root 'assets/cards.json'),
    (Join-Path $project_root 'assets/characters.json')
)
$glyph_text = -join (32..126 | ForEach-Object { [char]$_ })
foreach ($text_file in $text_files) {
    $glyph_text += [System.IO.File]::ReadAllText($text_file)
}
$glyphs = -join ($glyph_text.ToCharArray() | Where-Object { [int]$_ -ge 32 } | Sort-Object -Unique -CaseSensitive)
[System.IO.File]::WriteAllText((Join-Path $project_root 'assets/fonts/glyphs.txt'), $glyphs,
    [System.Text.UTF8Encoding]::new($false))
