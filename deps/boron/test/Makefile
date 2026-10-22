# Makefile 

.PHONY: test grind clean

test:
	@./run_test *.b

no-fibre:
	@GLOBIGNORE=fibre.b;./run_test *.b

grind:
	@./grind *.b

clean:
	@rm -f *.out
