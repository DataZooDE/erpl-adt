function bapi_user_get_detail
  importing
    value(username) like bapibname-bapibname
    value(cache_results) type flag_x default 'X'
    value(extuid_get) type bapiextuidget optional
  exporting
    value(logondata) like bapilogond
    value(defaults) like bapidefaul
    value(address) like bapiaddr3
    value(company) like bapiuscomp
    value(snc) like bapisncu
    value(ref_user) like bapirefus
    value(alias) like bapialias
    value(uclass) type bapiuclass
    value(lastmodified) type bapimoddat
    value(islocked) type bapislockd
    value(identity) type bapiidentity
    value(admindata) type bapiuseradmin
    value(description) type bapiusdesc
    value(tech_user) type bapitechuser
    value(sapuser_uuid) type bapiuseruuid
  tables
    parameter like bapiparam optional
    profiles like bapiprof optional
    activitygroups like bapiagr optional
    return like bapiret2
    addtel like bapiadtel optional
    addfax like bapiadfax optional
    addttx like bapiadttx optional
    addtlx like bapiadtlx optional
    addsmtp like bapiadsmtp optional
    addrml like bapiadrml optional
    addx400 like bapiadx400 optional
    addrfc like bapiadrfc optional
    addprt like bapiadprt optional
    addssf like bapiadssf optional
    adduri like bapiaduri optional
    addpag like bapiadpag optional
    addcomrem like bapicomrem optional
    parameter1 like bapiparam1 optional
    groups like bapigroups optional
    uclasssys like bapiuclasssys optional
    extidhead like bapiusextidhead optional
    extidpart like bapiusextidpart optional
    systems like bapircvsys optional
    extuid like bapiextuid optional
    sapuser_uuid_hist like bapiuseruuidhist optional
    usattribute like bapiusattribute optional.





  " Translate Key to Upper case
  set locale language sy-langu.
  translate username to upper case.

  " Initialize all exporting parameters
  clear: logondata, defaults,  address,        company,   snc,       ref_user
       , alias,     uclass,    lastmodified,   islocked,  identity,  admindata
       , tech_user
       .
  clear: parameter[], profiles[],  activitygroups[], return[],    addtel[],    addfax[]
       , addttx[],    addtlx[],    addsmtp[],        addrml[],    addx400[],   addrfc[]
       , addprt[],    addssf[],    adduri[],         addpag[],    addcomrem[], parameter1[]
       , groups[],    uclasssys[], extidhead[],      extidpart[], systems[]
       .

  " --- Declaration of BAPI related data ---
  data: ls_agr                    type          bapiagr
       ,ls_prof                   type          bapiprof
       ,ls_param                  type          bapiparam
       ,ls_param1                 type          bapiparam1
       ,ls_groups                 type          bapigroups
       ,ls_uclasssys              type          bapiuclasssys
       ,ls_systems                type          bapircvsys
       ,ls_return                 type          bapiret2
       ,lt_return                 type          bapirettab
       .

  " --- Declaration of data related to transactional concept ---
  data: lt_bname                  type          suid_tt_bname
       ,ls_bname                  type          suid_st_bname
       ,lx_suid_identity          type ref to   cx_suid_identity
       ,lt_node_root              type          suid_tt_node_root
       ,lr_node_root              type ref to   suid_st_node_root
       ,lo_msg_buffer             type ref to   if_suid_msg_buffer
       .

  " --- Declaration of Identity related data ---
  data: ls_logondata              type          suid_st_node_logondata
       ,ls_defaults               type          suid_st_node_defaults
       ,lv_kostl                  type          xukostl
       ,ls_snc                    type          suid_st_node_snc
       ,ls_refuser                type          suid_st_node_reference_user
       ,ls_uclass                 type          suid_st_node_uclass
       ,lt_roles                  type          suid_tt_node_roles
       ,lr_roles                  type ref to   suid_st_node_role
       ,lt_role_details           type          suid_tt_node_role_details
       ,lr_role_details           type ref to   suid_st_node_role_detail
