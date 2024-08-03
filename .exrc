set makeprg=./make
autocmd BufNewFile,BufRead *
	\   if expand("%") =~ "[A-Z]\\+"
	\ | 	setlocal expandtab textwidth=73
	\ | endif
