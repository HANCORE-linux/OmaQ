# Emoji asset attribution

The 22 PNG files in this directory are derived from glyph images embedded in
Google's Noto Color Emoji font.

- Project: [Noto Emoji](https://github.com/googlefonts/noto-emoji)
- Version: `2.051`
- Source commit: [`8998f5dd683424a73e2314a8c1f1e359c19e8742`](https://github.com/googlefonts/noto-emoji/tree/8998f5dd683424a73e2314a8c1f1e359c19e8742)
- Source file: [`fonts/NotoColorEmoji.ttf`](https://github.com/googlefonts/noto-emoji/blob/8998f5dd683424a73e2314a8c1f1e359c19e8742/fonts/NotoColorEmoji.ttf)
- Source-file SHA-256: `72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b`
- Copyright: Copyright 2013 Google LLC
- License: SIL Open Font License 1.1, with no Reserved Font Name declared (`OFL-1.1-no-RFN`)
- License text: [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt)

## Transformation

`scripts/extract-emoji.py` removes U+FE0F from the lookup sequence, maps each
remaining single code point through the font's `cmap`, and extracts its native
PNG from a CBDT format-17 record. ImageMagick then centers that PNG on a
transparent 136×136 canvas and resizes it to 64×64 with the Lanczos filter:

```text
magick SOURCE.png -background none -gravity center -extent 136x136 \
  -filter Lanczos -resize 64x64 -strip OUTPUT.png
```

The distributed files were produced with the Arch Linux package
`imagemagick 7.1.2.30-1`. The final `-strip` removes generated timestamps and
other ancillary metadata. Different ImageMagick releases can still encode
identical pixels into different PNG bytes, so the output hashes below identify
the artifacts that OmaQ distributes. The original hashes identify the PNG payloads
extracted from the pinned font before resizing.

| File | Embedded source PNG SHA-256 | Distributed PNG SHA-256 |
|---|---|---|
| `1f389.png` | `ccdba3c569ee5cee47ebc384cf14cae46d7a9b4d505dee5a63737e3d5757f393` | `fd1d97d9d108c6c471416f2e3da6b851ef5818b504ca5140b9f20758d0e9a48a` |
| `1f440.png` | `b95e5e9d2ef86b6bfb9c95c50cf840ba7c49b43f03d6953af7161d64738a70ef` | `6d8faa5624b97bb261160a43c92b14631a81809cd70947c269c982c999c2125b` |
| `1f44b.png` | `72d87529dfcd68a79c237620009b3754e0c8b48788cdf98799a09036e63d9f09` | `7cf812a16517074d30479e093364305b59fb2a059e059d74f07a594aa7647ac9` |
| `1f44d.png` | `7b08f9d9faaa5aba12d436392aafdef323745056e00cc9be411b3f1733801cf5` | `bae2fb9c29aee988b379b798c8663ede84fc849155536538f4c264faf8052575` |
| `1f44e.png` | `94b4f7a1a3ff160eae0dee9f82ff184f635cc28f6ff86ac1445f6f5670a3c252` | `fe557ff3970c425d6c5fe6b0406a7906dac9be91ea2d94f30b3f0f6fa9e4c94a` |
| `1f4af.png` | `1225ab6d9cf921f2ed5e645884a9990e2484d1801b4e0bf43d9a4355d8adda1a` | `07e2c3c67027473977b533a46a4a7330d9e9556a984e901fbf3499f78c2d8ea8` |
| `1f525.png` | `e86e8f9e81c724b821f217c32a0a54eee077bbae04d61e7c17c360053628f71d` | `39895867d7c8bf3d1cd2ee360dac7ef65684283e438c840df97796afd37ad94c` |
| `1f600.png` | `10d6305ce9241ddf4a53ee4f130999922b1da72115b44809231d660da57c18b2` | `0dcc80477d4edd14f7c0b7a541604cf54895244e4035f5726280ff7196794ca3` |
| `1f602.png` | `b96bc25e1b703b4d02824ebea92cc3aee17bd37b048629dfcf52c373b81ddc00` | `f822343d9115fa8c0ec20f6b187461cd767086af770e60b964b1012b0b21849d` |
| `1f605.png` | `c2e82e8c259242be618aa8912ae84b6dba1b82aeaadecaaf203a0053a3517dc8` | `00f8eec69c4863360ed2f8a7780438811bb7db7ad79167bafbd2aa13aff1eb6d` |
| `1f609.png` | `3dca1f7f42014a66529c6c6c507d8d8fc6c0882e8ef58f0ff49badbf8a229c2d` | `746ccdf91f2e1f94f1f75c8e1a247867b3cf6c1ed835c18ef5238af3e2cba2cb` |
| `1f60d.png` | `f5520fbddae21d9b3126f54f438fac420b5b698167c9ca33bd74f3ec23fff424` | `1fb44611b55088526867ae5892969ad681b7da0f036b184ac2d776b5b9f24a5f` |
| `1f621.png` | `253e3ab5fd2a6dba8176eac069a724203a0af10944426a056924357d1345454e` | `4f42446ba766351c32ae85ea2acc64523bfd90b2c2d9e946a14acbb1c23263a4` |
| `1f622.png` | `9391b8218ffb5d903c771398c59bef27751acf1b49c7247c63d4252b842ba4cc` | `0423a85ef1dab4920187fc52dd77889c8d65da43d7e5ac75c47a476f495331fb` |
| `1f62e.png` | `193e2427f3159e28d15c5e2bf347e2d016c9ce90b47e458472fbe1d0c8d6302e` | `8d82a8674a4219b4ac3db452cede3f8bc02710e05fc287d44fdb7caf64b6b69c` |
| `1f642.png` | `9c296a35ac6cbe028a460b7683e4726bb96cd0a20b771df643e10f03ab263fd7` | `9a3c8cb6f598068e326fa0bc31d9cd2b954eecdc6bc45869d7df357aca6bc751` |
| `1f64c.png` | `da9355a11041138aab6f33359543b8ae83092780b8ca0439a52245c58bcf0455` | `1d5ff643951a9feef5289d48922cbac8228e777717cd9dcc28ccc0ad64d4c91d` |
| `1f64f.png` | `0d0e64df9289619a592cc6346c184d93d1ef3023c38d772d86eb059367ecce49` | `36a487bb6ea6d272b87c2347706016504e74a270b44f80d5dfd6f2a29c046b83` |
| `1f914.png` | `2ff20ef612fa6031d56564fb0e7cfb50cd90722145a98b8944661625e9ede569` | `c02843630ff2f09bc9300fa347fe6bceaf4cafe03cc31ad860aaf37d1e3ae91d` |
| `2705.png` | `8e3b1175d22f98ac3732eb67282f3f99951061de833be7c6e1bab05977158afc` | `a51e760d324a3a289e54be7e816f14af5bfb88df0643f517b30b77aaa235cdc3` |
| `2728.png` | `d50b1f3aad1a9e72a26cfabfcd179f9a4bc29f4379101ff27f20247dacf858dd` | `646dbb274b6b3724b88eec494c06412c9cadce643b496367277f813124efe097` |
| `2764.png` | `200134e6ecf39657dca13bc32b1be433305e98d2d460c68b413707f89a7bdc1f` | `fc2a26e20b4449789bac9404e9e808937034f27866a4b403203d71f214b876a2` |
