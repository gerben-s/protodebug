<tr>
  <td style="border-right-width: 0px; text-align: right;">
    <code>
      {{#Member_is_protected}}protected {{/Member_is_protected}}
      {{#Member_function}}
        {{#Function_has_template_parameter}}template {{/Function_has_template_parameter}}
        {{#Function_prefix}}{{Function_prefix}} {{/Function_prefix}}
        {{#Function_type}}{{Expression_linked_text}}{{/Function_type}}
      {{/Member_function}}
      {{#Member_variable}}
        {{#Variable_type}}{{Expression_linked_text}}{{/Variable_type}}
      {{/Member_variable}}
      {{#Member_enum_type}}
        enum
      {{/Member_enum_type}}
      {{#Member_typedef_type}}
        typedef
      {{/Member_typedef_type}}
    </code>
  </td>
  <td style="border-left-width: 0px"
      id="{{Member_anchor}}">
    <div style="padding-left: 16px; text-indent: -16px"><code>
      {{#Member_typedef_type}}
        {{#Typedef_type}}{{Expression_linked_text}} {{/Typedef_type}}
      {{/Member_typedef_type}}
      <b>{{Member_name}}</b>
      {{#Member_function}}
        ({{#Function_parameter}}
          {{#Function_not_first_parameter}}, {{/Function_not_first_parameter}}
          {{#Parameter_type}}{{Expression_linked_text}} {{/Parameter_type}}
          {{Parameter_name}}
          {{#Parameter_default_value}} = {{Expression_linked_text}}{{/Parameter_default_value}}
        {{/Function_parameter}})
        {{#Function_suffix}} {{Function_suffix}}{{/Function_suffix}}
      {{/Member_function}}
      {{#Member_variable}}
        {{#Variable_value}} = {{Expression_linked_text}}{{/Variable_value}}
      {{/Member_variable}}
      {{#Member_enum_type}}
        {{! nothing }}
      {{/Member_enum_type}}
      {{#Member_typedef_type}}
        {{Typedef_function_args:h}}
      {{/Member_typedef_type}}
    </code></div>
    {{#Member_brief}}
      <div style="font-style: italic; margin-top: 4px; margin-left: 16px;">
        {{Member_brief}}
        {{#Member_details}} <a href="#{{Member_anchor}}.details">more...</a>{{/Member_details}}
      </div>
    {{/Member_brief}}
  </td>
</tr>
