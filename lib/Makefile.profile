all:: profiled $(LIBRARY)_p.a

subdirs:: profiled

profiled:
	mkdir profiled

clean::
	$(RM) -rf profiled

$(LIBRARY)_p.a: $(OBJS)
	(if test -r $@; then $(RM) -f $@.bak && $(MV) $@ $@.bak; fi)
	(cd profiled; $(ARUPD) ../$@ $(OBJS))
	-$(RANLIB) $@
	$(RM) -f ../$@
	$(LN) $@ ../$@
