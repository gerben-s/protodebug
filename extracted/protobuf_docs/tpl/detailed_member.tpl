{{#Member_details}} {{! <- We only want to display a detailed section if we have
                           a description. }}

<hr>

<h3 id="{{Member_anchor}}.details"><code>
  {{#Member_is_protected}}protected {{/Member_is_protected}}
  {{#Member_function}}
    {{#Function_has_template_parameter}}template {{/Function_has_template_parameter}}
    {{#Function_prefix}}{{Function_prefix}} {{/Function_prefix}}
    {{#Function_type}}{{Expression_linked_text}} {{/Function_type}}
    {{#Function_type_is_long}}<br>&nbsp;&nbsp;&nbsp;&nbsp;{{/Function_type_is_long}}
    {{#Member_scope}}{{Type_name}}{{/Member_scope}}::{{Member_name}}(
      {{#Function_parameter}}
        {{#Function_not_first_parameter}},{{/Function_not_first_parameter}}
        <br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;{{!}}
        {{#Parameter_type}}{{Expression_linked_text}} {{/Parameter_type}}
        {{Parameter_name}}
        {{#Parameter_default_value}} = {{Expression_linked_text}}{{/Parameter_default_value}}
      {{/Function_parameter}})
      {{#Function_suffix}} {{Function_suffix}}{{/Function_suffix}}
  {{/Member_function}}
  {{#Member_variable}}
    {{#Variable_type}}{{Expression_linked_text}}{{/Variable_type}}
    {{#Member_scope}}{{Type_name}}{{/Member_scope}}::{{Member_name}}{{#Variable_value}} = {{Expression_linked_text}}{{/Variable_value}}
  {{/Member_variable}}
  {{#Member_enum_type}}
    enum {{#Member_scope}}{{Type_name}}{{/Member_scope}}::{{Member_name}} {<br>
    {{#Enum_value}}
      {{#Enum_not_first_value}},<br>{{/Enum_not_first_value}}
      &nbsp;&nbsp;{{Value_name}}
      {{#Value_value}} = {{Expression_linked_text}}{{/Value_value}}
    {{/Enum_value}}
    <br>}
  {{/Member_enum_type}}
  {{#Member_typedef_type}}
    typedef {{#Member_scope}}{{Type_name}}{{/Member_scope}}::{{Member_name}}
    {{Typedef_function_args:h}}
  {{/Member_typedef_type}}
</code></h3>

<div style="margin-left: 16px">
  <p>{{Member_brief}}</p>
  {{Member_details}}

  {{#Member_enum_type}}
    <table>
      {{#Enum_value}}
        <tr>
          <td>{{Value_name}}</td>
          <td>
            {{#Value_details}}<p>{{/Value_details}}
            {{Value_brief}}
            {{#Value_details}}</p>{{/Value_details}}
            {{Value_details}}
          </td>
        </tr>
      {{/Enum_value}}
    </table>
  {{/Member_enum_type}}
</div>

{{/Member_details}}
