define view entity demo_cds_annotation_array
  as select from
    demo_expressions
    {
      @Consumption.filter.hierarchyBinding:
         [ { type: #ELEMENT, value: '...', variableSequence: 1 },
           { type: #CONSTANT, value: '...', variableSequence: 2 } ]
      id
    }
