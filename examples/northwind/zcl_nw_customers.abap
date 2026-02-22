CLASS zcl_nw_customers DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_nw_customers IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    DATA lt_data TYPE TABLE OF znw_customers WITH EMPTY KEY.

    lt_data = VALUE #(
      ( cust_id = 'ALFKI' comp_name = 'Alfreds Futterkiste'
        contact_name = 'Maria Anders' contact_title = 'Sales Representative'
        address = 'Obere Str. 57' city = 'Berlin' postal_code = '12209'
        country = 'Germany' phone = '030-0074321' fax = '030-0076545' )
      ( cust_id = 'ANATR' comp_name = 'Ana Trujillo Emparedados y helados'
        contact_name = 'Ana Trujillo' contact_title = 'Owner'
        address = 'Avda. de la Constitución 2222' city = 'México D.F.'
        postal_code = '05021' country = 'Mexico'
        phone = '(5) 555-4729' fax = '(5) 555-3745' )
      ( cust_id = 'ANTON' comp_name = 'Antonio Moreno Taquería'
        contact_name = 'Antonio Moreno' contact_title = 'Owner'
        address = 'Mataderos 2312' city = 'México D.F.'
        postal_code = '05023' country = 'Mexico' phone = '(5) 555-3932' )
      ( cust_id = 'AROUT' comp_name = 'Around the Horn'
        contact_name = 'Thomas Hardy' contact_title = 'Sales Representative'
        address = '120 Hanover Sq.' city = 'London' postal_code = 'WA1 1DP'
        country = 'UK' phone = '(171) 555-7788' fax = '(171) 555-6750' )
      ( cust_id = 'BERGS' comp_name = 'Berglunds snabbköp'
        contact_name = 'Christina Berglund' contact_title = 'Order Administrator'
        address = 'Berguvsvägen 8' city = 'Luleå' postal_code = 'S-958 22'
        country = 'Sweden' phone = '0921-12 34 65' fax = '0921-12 34 67' )
      ( cust_id = 'BLAUS' comp_name = 'Blauer See Delikatessen'
        contact_name = 'Hanna Moos' contact_title = 'Sales Representative'
        address = 'Forsterstr. 57' city = 'Mannheim' postal_code = '68306'
        country = 'Germany' phone = '0621-08460' fax = '0621-08924' )
      ( cust_id = 'BLONP' comp_name = 'Blondesddsl père et fils'
        contact_name = 'Frédérique Citeaux' contact_title = 'Marketing Manager'
        address = '24, place Kléber' city = 'Strasbourg' postal_code = '67000'
        country = 'France' phone = '88.60.15.31' fax = '88.60.15.32' )
      ( cust_id = 'BOLID' comp_name = 'Bólido Comidas preparadas'
        contact_name = 'Martín Sommer' contact_title = 'Owner'
        address = 'C/ Araquil, 67' city = 'Madrid' postal_code = '28023'
        country = 'Spain' phone = '(91) 555 22 82' fax = '(91) 555 91 99' )
      ( cust_id = 'BONAP' comp_name = 'Bon app'''
        contact_name = 'Laurence Lebihan' contact_title = 'Owner'
        address = '12, rue des Bouchers' city = 'Marseille' postal_code = '13008'
        country = 'France' phone = '91.24.45.40' fax = '91.24.45.41' )
      ( cust_id = 'BOTTM' comp_name = 'Bottom-Dollar Markets'
        contact_name = 'Elizabeth Lincoln' contact_title = 'Accounting Manager'
        address = '23 Tsawassen Blvd.' city = 'Tsawassen' region = 'BC'
        postal_code = 'T2F 8M4' country = 'Canada'
        phone = '(604) 555-4729' fax = '(604) 555-3745' )
      ( cust_id = 'BSBEV' comp_name = 'B''s Beverages'
        contact_name = 'Victoria Ashworth' contact_title = 'Sales Representative'
        address = 'Fauntleroy Circus' city = 'London' postal_code = 'EC2 5NT'
        country = 'UK' phone = '(171) 555-1212' )
      ( cust_id = 'CACTU' comp_name = 'Cactus Comidas para llevar'
        contact_name = 'Patricio Simpson' contact_title = 'Sales Agent'
        address = 'Cerrito 333' city = 'Buenos Aires' postal_code = '1010'
        country = 'Argentina' phone = '(1) 135-5555' fax = '(1) 135-4892' )
      ( cust_id = 'CENTC' comp_name = 'Centro comercial Moctezuma'
        contact_name = 'Francisco Chang' contact_title = 'Marketing Manager'
        address = 'Sierras de Granada 9993' city = 'México D.F.'
        postal_code = '05022' country = 'Mexico'
        phone = '(5) 555-3392' fax = '(5) 555-7293' )
      ( cust_id = 'CHOPS' comp_name = 'Chop-suey Chinese'
        contact_name = 'Yang Wang' contact_title = 'Owner'
        address = 'Hauptstr. 29' city = 'Bern' postal_code = '3012'
        country = 'Switzerland' phone = '0452-076545' )
      ( cust_id = 'COMMI' comp_name = 'Comércio Mineiro'
        contact_name = 'Pedro Afonso' contact_title = 'Sales Associate'
        address = 'Av. dos Lusíadas, 23' city = 'São Paulo' region = 'SP'
        postal_code = '05432-043' country = 'Brazil' phone = '(11) 555-7647' )
      ( cust_id = 'CONSH' comp_name = 'Consolidated Holdings'
        contact_name = 'Elizabeth Brown' contact_title = 'Sales Representative'
        address = 'Berkeley Gardens 12  Brewery' city = 'London'
        postal_code = 'WX1 6LT' country = 'UK'
        phone = '(171) 555-2282' fax = '(171) 555-9199' )
      ( cust_id = 'DRACD' comp_name = 'Drachenblut Delikatessen'
        contact_name = 'Sven Ottlieb' contact_title = 'Order Administrator'
        address = 'Walserweg 21' city = 'Aachen' postal_code = '52066'
        country = 'Germany' phone = '0241-039123' fax = '0241-059428' )
      ( cust_id = 'DUMON' comp_name = 'Du monde entier'
        contact_name = 'Janine Labrune' contact_title = 'Owner'
        address = '67, rue des Cinquante Otages' city = 'Nantes'
        postal_code = '44000' country = 'France'
        phone = '40.67.88.88' fax = '40.67.88.89' )
      ( cust_id = 'EASTC' comp_name = 'Eastern Connection'
        contact_name = 'Ann Devon' contact_title = 'Sales Agent'
        address = '35 King George' city = 'London' postal_code = 'WX3 6FW'
        country = 'UK' phone = '(171) 555-0297' fax = '(171) 555-3373' )
      ( cust_id = 'ERNSH' comp_name = 'Ernst Handel'
        contact_name = 'Roland Mendel' contact_title = 'Sales Manager'
        address = 'Kirchgasse 6' city = 'Graz' postal_code = '8010'
        country = 'Austria' phone = '7675-3425' fax = '7675-3426' )
      ( cust_id = 'FAMIA' comp_name = 'Familia Arquibaldo'
        contact_name = 'Aria Cruz' contact_title = 'Marketing Assistant'
        address = 'Rua Orós, 92' city = 'São Paulo' region = 'SP'
        postal_code = '05442-030' country = 'Brazil' phone = '(11) 555-9857' )
      ( cust_id = 'FISSA' comp_name = 'FISSA Fabrica Inter. Salchichas S.A.'
        contact_name = 'Diego Roel' contact_title = 'Accounting Manager'
        address = 'C/ Moralzarzal, 86' city = 'Madrid' postal_code = '28034'
        country = 'Spain' phone = '(91) 555 94 44' fax = '(91) 555 55 93' )
      ( cust_id = 'FOLIG' comp_name = 'Folies gourmandes'
        contact_name = 'Martine Rancé' contact_title = 'Assistant Sales Agent'
        address = '184, chaussée de Tournai' city = 'Lille' postal_code = '59000'
        country = 'France' phone = '20.16.10.16' fax = '20.16.10.17' )
      ( cust_id = 'FOLKO' comp_name = 'Folk och fä HB'
        contact_name = 'Maria Larsson' contact_title = 'Owner'
        address = 'Åkergatan 24' city = 'Bräcke' postal_code = 'S-844 67'
        country = 'Sweden' phone = '0695-34 67 21' )
      ( cust_id = 'FRANK' comp_name = 'Frankenversand'
        contact_name = 'Peter Franken' contact_title = 'Marketing Manager'
        address = 'Berliner Platz 43' city = 'München' postal_code = '80805'
        country = 'Germany' phone = '089-0877310' fax = '089-0877451' )
      ( cust_id = 'FRANR' comp_name = 'France restauration'
        contact_name = 'Carine Schmitt' contact_title = 'Marketing Manager'
        address = '54, rue Royale' city = 'Nantes' postal_code = '44000'
        country = 'France' phone = '40.32.21.21' fax = '40.32.21.20' )
      ( cust_id = 'FRANS' comp_name = 'Franchi S.p.A.'
        contact_name = 'Paolo Accorti' contact_title = 'Sales Representative'
        address = 'Via Monte Bianco 34' city = 'Torino' postal_code = '10100'
        country = 'Italy' phone = '011-4988260' fax = '011-4988261' )
      ( cust_id = 'FURIB' comp_name = 'Furia Bacalhau e Frutos do Mar'
        contact_name = 'Lino Rodriguez' contact_title = 'Sales Manager'
        address = 'Jardim das rosas n. 32' city = 'Lisboa' postal_code = '1675'
        country = 'Portugal' phone = '(1) 354-2534' fax = '(1) 354-2535' )
      ( cust_id = 'GALED' comp_name = 'Galería del gastrónomo'
        contact_name = 'Eduardo Saavedra' contact_title = 'Marketing Manager'
        address = 'Rambla de Cataluña, 23' city = 'Barcelona'
        postal_code = '08022' country = 'Spain'
        phone = '(93) 203 4560' fax = '(93) 203 4561' )
      ( cust_id = 'GODOS' comp_name = 'Godos Cocina Típica'
        contact_name = 'José Pedro Freyre' contact_title = 'Sales Manager'
        address = 'C/ Romero, 33' city = 'Sevilla' postal_code = '41101'
        country = 'Spain' phone = '(95) 555 82 82' )
      ( cust_id = 'GOURL' comp_name = 'Gourmet Lanchonetes'
        contact_name = 'André Fonseca' contact_title = 'Sales Associate'
        address = 'Av. Brasil, 442' city = 'Campinas' region = 'SP'
        postal_code = '04876-786' country = 'Brazil' phone = '(11) 555-9482' )
      ( cust_id = 'GREAL' comp_name = 'Great Lakes Food Market'
        contact_name = 'Howard Snyder' contact_title = 'Marketing Manager'
        address = '2732 Baker Blvd.' city = 'Eugene' region = 'OR'
        postal_code = '97403' country = 'USA' phone = '(503) 555-7555' )
      ( cust_id = 'GROSR' comp_name = 'GROSELLA-Restaurante'
        contact_name = 'Manuel Pereira' contact_title = 'Owner'
        address = '5ª Ave. Los Palos Grandes' city = 'Caracas' region = 'DF'
        postal_code = '1081' country = 'Venezuela'
        phone = '(2) 283-2951' fax = '(2) 283-3397' )
      ( cust_id = 'HANAR' comp_name = 'Hanari Carnes'
        contact_name = 'Mario Pontes' contact_title = 'Accounting Manager'
        address = 'Rua do Paço, 67' city = 'Rio de Janeiro' region = 'RJ'
        postal_code = '05454-876' country = 'Brazil'
        phone = '(21) 555-0091' fax = '(21) 555-8765' )
      ( cust_id = 'HILAA' comp_name = 'HILARION-Abastos'
        contact_name = 'Carlos Hernández' contact_title = 'Sales Representative'
        address = 'Carrera 22 con Ave. Carlos Soublette #8-35'
        city = 'San Cristóbal' region = 'Táchira'
        postal_code = '5022' country = 'Venezuela'
        phone = '(5) 555-1340' fax = '(5) 555-1948' )
      ( cust_id = 'HUNGC' comp_name = 'Hungry Coyote Import Store'
        contact_name = 'Yoshi Latimer' contact_title = 'Sales Representative'
        address = 'City Center Plaza 516 Main St.' city = 'Elgin' region = 'OR'
        postal_code = '97827' country = 'USA'
        phone = '(503) 555-6874' fax = '(503) 555-2376' )
      ( cust_id = 'HUNGO' comp_name = 'Hungry Owl All-Night Grocers'
        contact_name = 'Patricia McKenna' contact_title = 'Sales Associate'
        address = '8 Johnstown Road' city = 'Cork' region = 'Co. Cork'
        country = 'Ireland' phone = '2967 542' fax = '2967 3333' )
      ( cust_id = 'ISLAT' comp_name = 'Island Trading'
        contact_name = 'Helen Bennett' contact_title = 'Marketing Manager'
        address = 'Garden House Crowther Way' city = 'Cowes'
        region = 'Isle of Wight' postal_code = 'PO31 7PJ'
        country = 'UK' phone = '(198) 555-8888' )
      ( cust_id = 'KOENE' comp_name = 'Königlich Essen'
        contact_name = 'Philip Cramer' contact_title = 'Sales Associate'
        address = 'Maubelstr. 90' city = 'Brandenburg' postal_code = '14776'
        country = 'Germany' phone = '0555-09876' )
      ( cust_id = 'LACOR' comp_name = 'La corne d''abondance'
        contact_name = 'Daniel Tonini' contact_title = 'Sales Representative'
        address = '67, avenue de l''Europe' city = 'Versailles'
        postal_code = '78000' country = 'France'
        phone = '30.59.84.10' fax = '30.59.85.11' )
      ( cust_id = 'LAMAI' comp_name = 'La maison d''Asie'
        contact_name = 'Annette Roulet' contact_title = 'Sales Manager'
        address = '1 rue Alsace-Lorraine' city = 'Toulouse'
        postal_code = '31000' country = 'France'
        phone = '61.77.61.10' fax = '61.77.61.11' )
      ( cust_id = 'LAUGB' comp_name = 'Laughing Bacchus Wine Cellars'
        contact_name = 'Yoshi Tannamuri' contact_title = 'Marketing Assistant'
        address = '1900 Oak St.' city = 'Vancouver' region = 'BC'
        postal_code = 'V3F 2K1' country = 'Canada'
        phone = '(604) 555-3392' fax = '(604) 555-7293' )
      ( cust_id = 'LAZYK' comp_name = 'Lazy K Kountry Store'
        contact_name = 'John Steel' contact_title = 'Marketing Manager'
        address = '12 Orchestra Terrace' city = 'Walla Walla' region = 'WA'
        postal_code = '99362' country = 'USA'
        phone = '(509) 555-7969' fax = '(509) 555-6221' )
      ( cust_id = 'LEHMS' comp_name = 'Lehmanns Marktstand'
        contact_name = 'Renate Messner' contact_title = 'Sales Representative'
        address = 'Magazinweg 7' city = 'Frankfurt a.M.' postal_code = '60528'
        country = 'Germany' phone = '069-0245984' fax = '069-0245874' )
      ( cust_id = 'LETSS' comp_name = 'Let''s Stop N Shop'
        contact_name = 'Jaime Yorres' contact_title = 'Owner'
        address = '87 Polk St. Suite 5' city = 'San Francisco' region = 'CA'
        postal_code = '94117' country = 'USA' phone = '(415) 555-5938' )
      ( cust_id = 'LILAS' comp_name = 'LILA-Supermercado'
        contact_name = 'Carlos González' contact_title = 'Accounting Manager'
        address = 'Carrera 52 con Ave. Bolívar #65-98 Llano Largo'
        city = 'Barquisimeto' region = 'Lara'
        postal_code = '3508' country = 'Venezuela'
        phone = '(9) 331-6954' fax = '(9) 331-7256' )
      ( cust_id = 'LINOD' comp_name = 'LINO-Delicateses'
        contact_name = 'Felipe Izquierdo' contact_title = 'Owner'
        address = 'Ave. 5 de Mayo Porlamar' city = 'I. de Margarita'
        region = 'Nueva Esparta' postal_code = '4980' country = 'Venezuela'
        phone = '(8) 34-56-12' fax = '(8) 34-93-93' )
      ( cust_id = 'LONEP' comp_name = 'Lonesome Pine Restaurant'
        contact_name = 'Fran Wilson' contact_title = 'Sales Manager'
        address = '89 Chiaroscuro Rd.' city = 'Portland' region = 'OR'
        postal_code = '97219' country = 'USA'
        phone = '(503) 555-9573' fax = '(503) 555-9646' )
      ( cust_id = 'MAGAA' comp_name = 'Magazzini Alimentari Riuniti'
        contact_name = 'Giovanni Rovelli' contact_title = 'Marketing Manager'
        address = 'Via Ludovico il Moro 22' city = 'Bergamo'
        postal_code = '24100' country = 'Italy'
        phone = '035-640230' fax = '035-640231' )
      ( cust_id = 'MAISD' comp_name = 'Maison Dewey'
        contact_name = 'Catherine Dewey' contact_title = 'Sales Agent'
        address = 'Rue Joseph-Bens 532' city = 'Bruxelles'
        postal_code = 'B-1180' country = 'Belgium'
        phone = '(02) 201 24 67' fax = '(02) 201 24 68' )
      ( cust_id = 'MEREP' comp_name = 'Mère Paillarde'
        contact_name = 'Jean Fresnière' contact_title = 'Marketing Assistant'
        address = '43 rue St. Laurent' city = 'Montréal' region = 'Québec'
        postal_code = 'H1J 1C3' country = 'Canada'
        phone = '(514) 555-8054' fax = '(514) 555-8055' )
      ( cust_id = 'MORGK' comp_name = 'Morgenstern Gesundkost'
        contact_name = 'Alexander Feuer' contact_title = 'Marketing Assistant'
        address = 'Heerstr. 22' city = 'Leipzig' postal_code = '04179'
        country = 'Germany' phone = '0342-023176' )
      ( cust_id = 'NORTS' comp_name = 'North/South'
        contact_name = 'Simon Crowther' contact_title = 'Sales Associate'
        address = 'South House 300 Queensbridge' city = 'London'
        postal_code = 'SW7 1RZ' country = 'UK'
        phone = '(171) 555-7733' fax = '(171) 555-2530' )
      ( cust_id = 'OCEAN' comp_name = 'Océano Atlántico Ltda.'
        contact_name = 'Yvonne Moncada' contact_title = 'Sales Agent'
        address = 'Ing. Gustavo Moncada 8585 Piso 20-A' city = 'Buenos Aires'
        postal_code = '1010' country = 'Argentina'
        phone = '(1) 135-5333' fax = '(1) 135-5535' )
      ( cust_id = 'OLDWO' comp_name = 'Old World Delicatessen'
        contact_name = 'Russ Zipper' contact_title = 'Sales Representative'
        address = '2743 Bering St.' city = 'Anchorage' region = 'AK'
        postal_code = '99508' country = 'USA'
        phone = '(907) 555-7584' fax = '(907) 555-2880' )
      ( cust_id = 'OTTIK' comp_name = 'Ottilies Käseladen'
        contact_name = 'Henriette Pfalzheim' contact_title = 'Owner'
        address = 'Mehrheimerstr. 369' city = 'Köln' postal_code = '50739'
        country = 'Germany' phone = '0221-0644327' fax = '0221-0765721' )
      ( cust_id = 'PARIS' comp_name = 'Paris spécialités'
        contact_name = 'Marie Bertrand' contact_title = 'Owner'
        address = '265, boulevard Charonne' city = 'Paris' postal_code = '75012'
        country = 'France' phone = '(1) 42.34.22.66' fax = '(1) 42.34.22.77' )
      ( cust_id = 'PERIC' comp_name = 'Pericles Comidas clásicas'
        contact_name = 'Guillermo Fernández' contact_title = 'Sales Representative'
        address = 'Calle Dr. Jorge Cash 321' city = 'México D.F.'
        postal_code = '05033' country = 'Mexico'
        phone = '(5) 552-3745' fax = '(5) 545-3745' )
      ( cust_id = 'PICCO' comp_name = 'Piccolo und mehr'
        contact_name = 'Georg Pipps' contact_title = 'Sales Manager'
        address = 'Geislweg 14' city = 'Salzburg' postal_code = '5020'
        country = 'Austria' phone = '6562-9722' fax = '6562-9723' )
      ( cust_id = 'PRINI' comp_name = 'Princesa Isabel Vinhos'
        contact_name = 'Isabel de Castro' contact_title = 'Sales Representative'
        address = 'Estrada da saúde n. 58' city = 'Lisboa' postal_code = '1756'
        country = 'Portugal' phone = '(1) 356-5634' )
      ( cust_id = 'QUEDE' comp_name = 'Que Delícia'
        contact_name = 'Bernardo Batista' contact_title = 'Accounting Manager'
        address = 'Rua da Panificadora, 12' city = 'Rio de Janeiro' region = 'RJ'
        postal_code = '02389-673' country = 'Brazil'
        phone = '(21) 555-4252' fax = '(21) 555-4545' )
      ( cust_id = 'QUEEN' comp_name = 'Queen Cozinha'
        contact_name = 'Lúcia Carvalho' contact_title = 'Marketing Assistant'
        address = 'Alameda dos Canàrios, 891' city = 'São Paulo' region = 'SP'
        postal_code = '05487-020' country = 'Brazil' phone = '(11) 555-1189' )
      ( cust_id = 'QUICK' comp_name = 'QUICK-Stop'
        contact_name = 'Horst Kloss' contact_title = 'Accounting Manager'
        address = 'Taucherstraße 10' city = 'Cunewalde' postal_code = '01307'
        country = 'Germany' phone = '0372-035188' )
      ( cust_id = 'RANCH' comp_name = 'Rancho grande'
        contact_name = 'Sergio Gutiérrez' contact_title = 'Sales Representative'
        address = 'Av. del Libertador 900' city = 'Buenos Aires'
        postal_code = '1010' country = 'Argentina'
        phone = '(1) 123-5555' fax = '(1) 123-5556' )
      ( cust_id = 'RATTC' comp_name = 'Rattlesnake Canyon Grocery'
        contact_name = 'Paula Wilson'
        contact_title = 'Assistant Sales Representative'
        address = '2817 Milton Dr.' city = 'Albuquerque' region = 'NM'
        postal_code = '87110' country = 'USA'
        phone = '(505) 555-5939' fax = '(505) 555-3620' )
      ( cust_id = 'REGGC' comp_name = 'Reggiani Caseifici'
        contact_name = 'Maurizio Moroni' contact_title = 'Sales Associate'
        address = 'Strada Provinciale 124' city = 'Reggio Emilia'
        postal_code = '42100' country = 'Italy'
        phone = '0522-556721' fax = '0522-556722' )
      ( cust_id = 'RICAR' comp_name = 'Ricardo Adocicados'
        contact_name = 'Janete Limeira' contact_title = 'Assistant Sales Agent'
        address = 'Av. Copacabana, 267' city = 'Rio de Janeiro' region = 'RJ'
        postal_code = '02389-890' country = 'Brazil' phone = '(21) 555-3412' )
      ( cust_id = 'RICSU' comp_name = 'Richter Supermarkt'
        contact_name = 'Michael Holz' contact_title = 'Sales Manager'
        address = 'Grenzacherweg 237' city = 'Genève' postal_code = '1203'
        country = 'Switzerland' phone = '0897-034214' )
      ( cust_id = 'ROMEY' comp_name = 'Romero y tomillo'
        contact_name = 'Alejandra Camino' contact_title = 'Accounting Manager'
        address = 'Gran Vía, 1' city = 'Madrid' postal_code = '28001'
        country = 'Spain' phone = '(91) 745 6200' fax = '(91) 745 6210' )
      ( cust_id = 'SANTG' comp_name = 'Santé Gourmet'
        contact_name = 'Jonas Bergulfsen' contact_title = 'Owner'
        address = 'Erling Skakkes gate 78' city = 'Stavern' postal_code = '4110'
        country = 'Norway' phone = '07-98 92 35' fax = '07-98 92 47' )
      ( cust_id = 'SAVEA' comp_name = 'Save-a-lot Markets'
        contact_name = 'Jose Pavarotti' contact_title = 'Sales Representative'
        address = '187 Suffolk Ln.' city = 'Boise' region = 'ID'
        postal_code = '83720' country = 'USA' phone = '(208) 555-8097' )
      ( cust_id = 'SEVES' comp_name = 'Seven Seas Imports'
        contact_name = 'Hari Kumar' contact_title = 'Sales Manager'
        address = '90 Wadhurst Rd.' city = 'London' postal_code = 'OX15 4NB'
        country = 'UK' phone = '(171) 555-1717' fax = '(171) 555-5646' )
      ( cust_id = 'SIMOB' comp_name = 'Simons bistro'
        contact_name = 'Jytte Petersen' contact_title = 'Owner'
        address = 'Vinbæltet 34' city = 'København' postal_code = '1734'
        country = 'Denmark' phone = '31 12 34 56' fax = '31 13 35 57' )
      ( cust_id = 'SPECD' comp_name = 'Spécialités du monde'
        contact_name = 'Dominique Perrier' contact_title = 'Marketing Manager'
        address = '25, rue Lauriston' city = 'Paris' postal_code = '75016'
        country = 'France' phone = '(1) 47.55.60.10' fax = '(1) 47.55.60.20' )
      ( cust_id = 'SPLIR' comp_name = 'Split Rail Beer & Ale'
        contact_name = 'Art Braunschweiger' contact_title = 'Sales Manager'
        address = 'P.O. Box 555' city = 'Lander' region = 'WY'
        postal_code = '82520' country = 'USA'
        phone = '(307) 555-4680' fax = '(307) 555-6525' )
      ( cust_id = 'SUBAC' comp_name = 'Suprêmes délices'
        contact_name = 'Pascale Cartrain' contact_title = 'Accounting Manager'
        address = 'Boulevard Tirou, 255' city = 'Charleroi' postal_code = 'B-6000'
        country = 'Belgium' phone = '(071) 23 67 22 20' fax = '(071) 23 67 22 21' )
      ( cust_id = 'THEBI' comp_name = 'The Big Cheese'
        contact_name = 'Liz Nixon' contact_title = 'Marketing Manager'
        address = '89 Jefferson Way Suite 2' city = 'Portland' region = 'OR'
        postal_code = '97201' country = 'USA' phone = '(503) 555-3612' )
      ( cust_id = 'THECR' comp_name = 'The Cracker Box'
        contact_name = 'Liu Wong' contact_title = 'Marketing Assistant'
        address = '55 Grizzly Peak Rd.' city = 'Butte' region = 'MT'
        postal_code = '59801' country = 'USA'
        phone = '(406) 555-5834' fax = '(406) 555-8083' )
      ( cust_id = 'TOMSP' comp_name = 'Toms Spezialitäten'
        contact_name = 'Karin Josephs' contact_title = 'Marketing Manager'
        address = 'Luisenstr. 48' city = 'Münster' postal_code = '44087'
        country = 'Germany' phone = '0251-031259' fax = '0251-035695' )
      ( cust_id = 'TORTU' comp_name = 'Tortuga Restaurante'
        contact_name = 'Miguel Angel Paolino' contact_title = 'Owner'
        address = 'Avda. Azteca 123' city = 'México D.F.' postal_code = '05033'
        country = 'Mexico' phone = '(5) 555-2933' )
      ( cust_id = 'TRADH' comp_name = 'Tradição Hipermercados'
        contact_name = 'Anabela Domingues' contact_title = 'Sales Representative'
        address = 'Av. Inês de Castro, 414' city = 'São Paulo' region = 'SP'
        postal_code = '05634-030' country = 'Brazil'
        phone = '(11) 555-2167' fax = '(11) 555-2168' )
      ( cust_id = 'TRAIH' comp_name = 'Trail''s Head Gourmet Provisioners'
        contact_name = 'Helvetius Nagy' contact_title = 'Sales Associate'
        address = '722 DaVinci Blvd.' city = 'Kirkland' region = 'WA'
        postal_code = '98034' country = 'USA'
        phone = '(206) 555-8257' fax = '(206) 555-2174' )
      ( cust_id = 'VAFFE' comp_name = 'Vaffeljernet'
        contact_name = 'Palle Ibsen' contact_title = 'Sales Manager'
        address = 'Smagsloget 45' city = 'Århus' postal_code = '8200'
        country = 'Denmark' phone = '86 21 32 43' fax = '86 22 33 44' )
      ( cust_id = 'VICTE' comp_name = 'Victuailles en stock'
        contact_name = 'Mary Saveley' contact_title = 'Sales Agent'
        address = '2, rue du Commerce' city = 'Lyon' postal_code = '69004'
        country = 'France' phone = '78.32.54.86' fax = '78.32.54.87' )
      ( cust_id = 'VINET' comp_name = 'Vins et alcools Chevalier'
        contact_name = 'Paul Henriot' contact_title = 'Accounting Manager'
        address = '59 rue de l''Abbaye' city = 'Reims' postal_code = '51100'
        country = 'France' phone = '26.47.15.10' fax = '26.47.15.11' )
      ( cust_id = 'WANDK' comp_name = 'Die Wandernde Kuh'
        contact_name = 'Rita Müller' contact_title = 'Sales Representative'
        address = 'Adenauerallee 900' city = 'Stuttgart' postal_code = '70563'
        country = 'Germany' phone = '0711-020361' fax = '0711-035428' )
      ( cust_id = 'WARTH' comp_name = 'Wartian Herkku'
        contact_name = 'Pirkko Koskitalo' contact_title = 'Accounting Manager'
        address = 'Torikatu 38' city = 'Oulu' postal_code = '90110'
        country = 'Finland' phone = '981-443655' fax = '981-443655' )
      ( cust_id = 'WELLI' comp_name = 'Wellington Importadora'
        contact_name = 'Paula Parente' contact_title = 'Sales Manager'
        address = 'Rua do Mercado, 12' city = 'Resende' region = 'SP'
        postal_code = '08737-363' country = 'Brazil' phone = '(14) 555-8122' )
      ( cust_id = 'WHITC' comp_name = 'White Clover Markets'
        contact_name = 'Karl Jablonski' contact_title = 'Owner'
        address = '305 - 14th Ave. S. Suite 3B' city = 'Seattle' region = 'WA'
        postal_code = '98128' country = 'USA'
        phone = '(206) 555-4112' fax = '(206) 555-4115' )
      ( cust_id = 'WILMK' comp_name = 'Wilman Kala'
        contact_name = 'Matti Karttunen'
        contact_title = 'Owner/Marketing Assistant'
        address = 'Keskuskatu 45' city = 'Helsinki' postal_code = '21240'
        country = 'Finland' phone = '90-224 8858' fax = '90-224 8858' )
      ( cust_id = 'WOLZA' comp_name = 'Wolski Zajazd'
        contact_name = 'Zbyszek Piestrzeniewicz' contact_title = 'Owner'
        address = 'ul. Filtrowa 68' city = 'Warszawa' postal_code = '01-012'
        country = 'Poland' phone = '(26) 642-7012' fax = '(26) 642-7012' )
    ).

    DELETE FROM znw_customers.
    INSERT znw_customers FROM TABLE @lt_data.

    out->write( |Loaded { lines( lt_data ) } Northwind customers into ZNW_CUSTOMERS| ).

  ENDMETHOD.
ENDCLASS.
