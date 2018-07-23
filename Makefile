.SUFFIXES:

backend: backend/backend
frontend: index.html

site.js: frontend/site.c
	$(EMSDK)/emcc -o site.js -O3 --llvm-lto 1 --memory-init-file 0 -s USE_GLFW=3 -s WASM=0 -s NO_FILESYSTEM=1 -s FETCH=1 --closure 1 frontend/site.c -lm -lopenal

assets/%.o.woff2: $(filter-out %.o.woff2, $(wildcard assets/*.woff2))
	ls assets/*.woff2 | xargs -L 1 -I @ bash -c 'node_modules/.bin/webfont-crusher -i @ -o assets/ -n `basename @ .woff2`.o -f woff2 -g `printf %s {a..z} {A..Z} .\(\)`'\\\'

index.html: site.js frontend/site.html assets/avatar.png assets/*.o.woff2
	perl template.pl frontend/site.html temp.html
	node_modules/.bin/html-minifier --remove-optional-tags --remove-attribute-quotes --collapse-whitespace --minify-css 1 temp.html > index.html
	rm temp.html

backend/backend: backend/backend.c backend/update_tweets.c
	gcc -Ofast -Ibackend $(INCLUDE) -L/usr/lib/x86_64-linux-gnu/mit-krb5 -o backend/backend backend/mongoose/mongoose.c backend/backend.c -lpq -lpthread -lssl -lcrypto -lldap -lgssapi_krb5
	gcc -Ofast -Ibackend $(INCLUDE) -L/usr/lib/x86_64-linux-gnu/mit-krb5 -DMG_ENABLE_SSL -o backend/update_tweets backend/mongoose/mongoose.c backend/cJSON/cJSON.c backend/update_tweets.c -lpq -lpthread -lssl -lcrypto -lldap -lgssapi_krb5

frontend_deploy: frontend
	git stash

	git checkout --detach
	git reset --soft master
	git checkout master

	git reset
	git add -f index.html site.js assets/*.pcm assets/*.m4a assets/*.ogg
	git commit -m "deploy"
	git push origin master:master

	git checkout --detach
	git reset --soft site
	git checkout site

	-git stash apply

backend_deploy:
	git push heroku site:master

clean:
	rm -f site.js index.html backend/backend backend/update_tweets assets/*.o.woff2
