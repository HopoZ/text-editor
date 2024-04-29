# text-editor
Try to write a text editor by myself with c,referring to https://viewsourcecode.org/snaptoken/kilo/  
我不知道为什么用中文模式会使ubuntu下的kitty下的temux下的nvim模式变得非常卡  

20240323  
前几天也搞定了ssh连接git  
搞定了zsh,kitty,tmux,nvim,这是工具的磨砺  
editor现在可以取消输入的回显，就像你在输密码时一样  
回显ascii和字符

主要干了点活，配置了nvim的代码功能，放弃了kitty，原生terminal挺好用的

2024 0404  
原生模式基本完成，刚刚vm突然卡死了，还以为是分配内存太少了，其实是windows内存爆了，卧槽firrrefox占了4G内存，比idea都多  

2024 0408  
一不留神就掠过四天光阴。
获取了当前命令行行数，各种奇怪的esc指令

2024 0410  
进度缓慢，还是得逼自己一把才行

2024 0411  
完成Raw input and output，要步入编辑器的正题了

2024 0414  
简单的viewer实现,Hello,world!

2024 0415  
简单的读取文件，现在只能读取首行

2024 0417  
完整读入文件

2024 0420  
为什么我完整读入不了文件，只能读取屏幕行数

2024 0423  
上一个问题是因为代码中敲入了不会报错的错误代码，这种误触很引人烦恼。现在又有个问题，nvim的yy到全局剪贴板好像失效了，真的莫名其妙。

2024 0429  
很好，我放弃给nvim配置太复杂的功能了，配置太浪费时间了，只用基本功能算了  
editor功能完成，快到结尾了，发现看待nvim之类的编辑器眼光都不同了，思考它们的细节是什么函数，挺有趣的。
vscode ms官方intelligence不好用，还是clangd这个lsp强一点



